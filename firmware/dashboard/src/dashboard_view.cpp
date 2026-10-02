#include "app.h"

// ---- Metric history ----
const int HISTORY = 240;  // samples kept per series: 8 minutes at 2s (Stacked and Focus show all of it)

struct Panel {
    const char *title;
    int numSeries;
    float fixedMax;           // > 0: fixed scale (e.g. 100 for %); 0: autoscale
    const char *labels[2];    // legend labels for 2-series panels
    const char *unit;         // unit of the series values, shown with the autoscale max
    float hist[2][HISTORY];
    int count;                // number of valid samples (<= HISTORY)
    int head;                 // index of the next write
    char value[24];           // headline value (Quad, Stacked, Focus)
    char big[12];             // large number (Tiles)
    char sub[32];             // detail line under the large number (Tiles)
};

static Panel panels[4] = {
    {"CPU/GPU", 2, 100.0f, {"cpu", "gpu"}, "%"},
    {"RAM", 1, 100.0f},
    {"DISK", 2, 0.0f, {"rd", "wr"}, "MB/s"},
    {"NET", 2, 0.0f, {"rx", "tx"}, "KB/s"},
};
static Panel &cpuPanel = panels[0];
static Panel &ramPanel = panels[1];
static Panel &diskPanel = panels[2];
static Panel &netPanel = panels[3];

static void pushSample(Panel &p, float a, float b = 0) {
    p.hist[0][p.head] = a;
    p.hist[1][p.head] = b;
    p.head = (p.head + 1) % HISTORY;
    if (p.count < HISTORY) p.count++;
}

// Value of series s, i samples back from the newest (i = 0 is newest).
static float histAt(const Panel &p, int s, int i) {
    return p.hist[s][(p.head - 1 - i + HISTORY) % HISTORY];
}

// Round up to 1, 2 or 5 x 10^n (minimum 1) so the autoscale max reads cleanly.
static float niceCeil(float v) {
    float step = 1.0f;
    while (step < v) {
        if (step * 2 >= v) return step * 2;
        if (step * 5 >= v) return step * 5;
        step *= 10;
    }
    return step;
}

// Scale for the newest n samples: fixed, or autoscaled to their peak.
static float scaleMax(const Panel &p, int n) {
    if (p.fixedMax > 0) return p.fixedMax;
    float peak = 0;
    for (int s = 0; s < p.numSeries; s++)
        for (int i = 0; i < min(n, p.count); i++) peak = max(peak, histAt(p, s, i));
    return niceCeil(peak);
}

static void formatRate(char *buf, size_t len, float kbps) {
    if (kbps < 1000) snprintf(buf, len, "%.0f KB/s", kbps);
    else snprintf(buf, len, "%.1f MB/s", kbps / 1000);
}

void ingestSample() {
    pushSample(cpuPanel, metrics.cpu, metrics.gpu);
    snprintf(cpuPanel.value, sizeof(cpuPanel.value), "%.0f%% / %.0f%%", metrics.cpu, metrics.gpu);
    snprintf(cpuPanel.big, sizeof(cpuPanel.big), "%.0f%%", metrics.cpu);
    snprintf(cpuPanel.sub, sizeof(cpuPanel.sub), "GPU %.0f%%  top: %.12s", metrics.gpu,
             metrics.numProcs ? metrics.procs[0].name : "-");

    pushSample(ramPanel, metrics.ram);
    snprintf(ramPanel.value, sizeof(ramPanel.value), "%.0f/%.0fG %.0f%%", metrics.ramUsed, metrics.ramTotal,
             metrics.ram);
    snprintf(ramPanel.big, sizeof(ramPanel.big), "%.0f%%", metrics.ram);
    snprintf(ramPanel.sub, sizeof(ramPanel.sub), "%.1f of %.0f GB used", metrics.ramUsed, metrics.ramTotal);

    pushSample(diskPanel, metrics.diskRead, metrics.diskWrite);
    float disk = metrics.diskRead + metrics.diskWrite;
    snprintf(diskPanel.value, sizeof(diskPanel.value), "%.0f%% used", metrics.disk);
    snprintf(diskPanel.big, sizeof(diskPanel.big), disk < 10 ? "%.1f MB/s" : "%.0f MB/s", disk);
    snprintf(diskPanel.sub, sizeof(diskPanel.sub), "rd %.1f  wr %.1f  %.0f%% used", metrics.diskRead,
             metrics.diskWrite, metrics.disk);

    pushSample(netPanel, metrics.netRx, metrics.netTx);
    formatRate(netPanel.value, sizeof(netPanel.value), metrics.netRx + metrics.netTx);
    formatRate(netPanel.big, sizeof(netPanel.big), metrics.netRx + metrics.netTx);
    snprintf(netPanel.sub, sizeof(netPanel.sub), "rx %.0f  tx %.0f KB/s", metrics.netRx, metrics.netTx);
}

// ---- Drawing helpers (absolute screen coordinates; see renderRegion) ----

// Graph of the newest n samples in (x, y, w, h), newest at the right edge.
static void drawGraph(const Panel &p, int x, int y, int w, int h, int n, bool grid) {
    float maxV = scaleMax(p, n);
    // Rounded frame around the plot area, with recessive quarter grid lines inside it.
    canvas.drawRoundRect(x - 3, y - 3, w + 6, h + 6, 5, theme.grid);
    if (grid) {
        for (int g = 1; g <= 3; g++) {
            int gy = y + h * g / 4;
            for (int gx = x; gx < x + w; gx += 4) canvas.drawPixel(gx, gy, theme.grid);
        }
    }
    bool thick = h >= 30;
    int shown = min(n, p.count);
    for (int s = 0; s < p.numSeries; s++) {
        int prevX = -1, prevY = 0;
        for (int i = shown - 1; i >= 0; i--) {
            float v = constrain(histAt(p, s, i), 0.0f, maxV);
            int px = x + w - 1 - (int)((float)i * (w - 1) / (n - 1));
            int py = y + h - 1 - (int)(v / maxV * (h - 2));
            if (prevX >= 0) {
                canvas.drawLine(prevX, prevY, px, py, theme.series[s]);
                if (thick) canvas.drawLine(prevX, prevY - 1, px, py - 1, theme.series[s]);
            }
            prevX = px;
            prevY = py;
        }
    }
}

// Legend: a swatch per series with label + latest value (text ink, never series-colored text).
// Returns the x just past the legend.
static int drawLegend(const Panel &p, int x, int y) {
    canvas.setStyle(1);
    canvas.setTextColor(theme.text2);
    for (int s = 0; s < p.numSeries && p.numSeries == 2; s++) {
        canvas.fillRect(x, y, 8, 8, theme.series[s]);
        char buf[20];
        float v = p.count ? histAt(p, s, 0) : 0.0f;
        snprintf(buf, sizeof(buf), v < 10 ? "%s %.1f" : "%s %.0f", p.labels[s], v);
        canvas.cursor(x + 12, y);
        canvas.print(buf);
        x += 12 + strlen(buf) * 6 + 10;
    }
    return x;
}

static void scaleText(const Panel &p, int n, char *buf, size_t len) {
    if (p.fixedMax > 0) buf[0] = 0;
    else snprintf(buf, len, "max %g %s", scaleMax(p, n), p.unit);
}

// Rounded card filling (x, y, w, h): a 2px gap in the separator color on every side, then the surface with
// rounded corners. Strip renders draw their clipped slice of the same card, so tall cards stay seamless.
static const int CARD_RADIUS = 8;

static void card(int x, int y, int w, int h) {
    canvas.fillRect(x, y, w, h, theme.separator);
    canvas.fillRoundRect(x + 2, y + 2, w - 4, h - 4, CARD_RADIUS, theme.surface);
}

static void panelBackground(int x, int y, int w, int h) {
    card(x, y, w, h);
}

// Standard panel: title + headline value, legend row, graph (Quad panels and the Focus main panel).
static void drawStandardPanel(const Panel &p, int x, int y, int w, int h, int n) {
    panelBackground(x, y, w, h);
    canvas.setStyle(2);
    canvas.setTextColor(theme.text2);
    canvas.cursor(x + 8, y + 7);
    canvas.print(p.title);
    canvas.setTextColor(theme.text);
    printRight(p.value, x + w - 8, y + 7);
    drawLegend(p, x + 8, y + 25);
    char buf[24];
    scaleText(p, n, buf, sizeof(buf));
    canvas.setStyle(1);
    canvas.setTextColor(theme.text2);
    if (w >= 200) printRight(buf, x + w - 8, y + 25);  // too narrow for both legend and scale
    drawGraph(p, x + 10, y + 38, w - 20, h - 47, n, true);
}

// Stacked strip: label column on the left, long graph on the right.
static void drawStrip(const Panel &p, int x, int y, int w, int h) {
    panelBackground(x, y, w, h);
    canvas.setStyle(2);
    canvas.setTextColor(theme.text2);
    canvas.cursor(x + 8, y + 5);
    canvas.print(p.title);
    canvas.setStyle(1);
    canvas.setTextColor(theme.text);
    canvas.cursor(x + 8, y + 24);
    canvas.print(p.value);
    if (p.numSeries == 2) {
        // Two legend lines stacked in the label column.
        canvas.setTextColor(theme.text2);
        for (int s = 0; s < 2; s++) {
            canvas.fillRect(x + 8, y + 35 + s * 11, 8, 8, theme.series[s]);
            char buf[20];
            float v = p.count ? histAt(p, s, 0) : 0.0f;
            snprintf(buf, sizeof(buf), v < 10 ? "%s %.1f" : "%s %.0f", p.labels[s], v);
            canvas.cursor(x + 20, y + 35 + s * 11);
            canvas.print(buf);
        }
    }
    const int gx = x + 120, gw = w - 128;
    drawGraph(p, gx, y + 7, gw, h - 15, HISTORY, true);
    char buf[24];
    scaleText(p, HISTORY, buf, sizeof(buf));
    if (buf[0]) {
        int bw = strlen(buf) * 6 + 4;
        canvas.fillRect(gx + 3, y + 8, bw, 10, theme.surface);
        canvas.setTextColor(theme.text2);
        canvas.cursor(gx + 5, y + 9);
        canvas.print(buf);
    }
}

// Tile: title, large number, detail line, sparkline.
static void drawTile(const Panel &p, int x, int y, int w, int h) {
    panelBackground(x, y, w, h);
    canvas.setStyle(2);
    canvas.setTextColor(theme.text2);
    canvas.cursor(x + 8, y + 6);
    canvas.print(p.title);
    canvas.setStyle(4);
    canvas.setTextColor(theme.text);
    canvas.cursor(x + 8, y + 27);
    canvas.print(p.big);
    canvas.setStyle(1);
    canvas.setTextColor(theme.text2);
    canvas.cursor(x + 8, y + 63);
    canvas.print(p.sub);
    drawGraph(p, x + 11, y + 77, w - 22, h - 85, 113, false);
}

// Small Focus-layout tile: title + value + sparkline. Tapping it makes it the focused metric.
static void drawMiniTile(const Panel &p, int x, int y, int w, int h) {
    panelBackground(x, y, w, h);
    canvas.setStyle(1);
    canvas.setTextColor(theme.text2);
    canvas.cursor(x + 8, y + 6);
    canvas.print(p.title);
    canvas.setStyle(2);
    canvas.setTextColor(theme.text);
    canvas.cursor(x + 8, y + 17);
    canvas.print(p.value);
    drawGraph(p, x + 11, y + 38, w - 22, h - 46, 60, false);
}

// ---- Process table ----
struct TableGeom {
    int y, rows, rowH;
};

struct Rect {
    int x, y, w, h;
};

// Portrait (320x480) when the screen is rotated that way; every layout has a landscape and a portrait geometry.
static bool portrait() {
    return tft.height() > tft.width();
}

static TableGeom tableGeom() {
    if (portrait()) {
        switch (settings.layout) {
        case LAYOUT_STACKED: return {256, 5, 19};
        case LAYOUT_FOCUS: return {210, 5, 19};
        case LAYOUT_TILES: return {220, 5, 19};
        default: return {288, 5, 19};
        }
    }
    if (settings.layout == LAYOUT_STACKED) return {232, 3, 24};
    return {210, 5, 19};
}

static const int TABLE_W = 320;
static const int CLOCK_X = TABLE_W;
static const int CLOCK_W = SCREEN_W - TABLE_W;
static const int TABLE_HEADER_H = 15;
static const int COL_BAR_X = 146;       // CPU bar, scaled to one full core (100%), clipped
static const int COL_BAR_W = 56;
static const int COL_CPU_RIGHT = 258;   // right edge of the CPU% value
static const int COL_MEM_RIGHT = 310;   // right edge of the MEM% value

// Bottom of the table card: the screen bottom in landscape (the clock sits beside it), the clock's top in portrait.
static int tableBottom() {
    TableGeom t = tableGeom();
    return portrait() ? t.y + TABLE_HEADER_H + t.rows * t.rowH + 4 : SCREEN_H;
}

// Landscape: beside the table (bottom right). Portrait: full width under the table.
static Rect clockRect() {
    TableGeom t = tableGeom();
    if (portrait()) return {0, tableBottom(), TABLE_W, tft.height() - tableBottom()};
    return {CLOCK_X, t.y, CLOCK_W, SCREEN_H - t.y};
}

static void tableCard() {
    int y = tableGeom().y;
    card(0, y, TABLE_W, tableBottom() - y);
}

static void drawTableHeader(int y) {
    tableCard();
    canvas.setStyle(1);
    canvas.setTextColor(theme.text2);
    canvas.cursor(8, y + 5);
    canvas.print("TOP PROCESSES");
    // Connection status (dot + label, so it does not rely on color alone).
    canvas.fillCircle(96, y + 8, 3, stale ? theme.critical : theme.good);
    canvas.cursor(103, y + 5);
    canvas.print(stale ? (samples ? "NO HOST DATA" : "WAITING FOR HOST") : "LIVE");
    printRight("CPU%", COL_CPU_RIGHT, y + 5);
    printRight("MEM%", COL_MEM_RIGHT, y + 5);
}

static void drawTableRow(int i, int y, int h) {
    tableCard();
    canvas.drawFastHLine(8, y, TABLE_W - 16, theme.grid);
    if (i >= metrics.numProcs) return;
    const Proc &p = metrics.procs[i];
    int ty = y + (h - 16) / 2 + 1;
    canvas.setStyle(2);
    canvas.setTextColor(theme.text);
    canvas.cursor(8, ty);
    printFit(p.name, COL_BAR_X - 14);
    int barW = (int)(constrain(p.cpu, 0.0f, 100.0f) / 100.0f * COL_BAR_W);
    canvas.fillRoundRect(COL_BAR_X, y + h / 2 - 4, COL_BAR_W, 8, 4, theme.grid);
    if (barW > 0) canvas.fillRoundRect(COL_BAR_X, y + h / 2 - 4, max(barW, 8), 8, 4, theme.series[0]);
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f", p.cpu);
    printRight(buf, COL_CPU_RIGHT, ty);
    snprintf(buf, sizeof(buf), "%.1f", p.mem);
    printRight(buf, COL_MEM_RIGHT, ty);
}

// Clock tile: large time, AM/PM, and the date.
static void drawClockTile(Rect r) {
    int y = r.y, h = r.h;
    card(r.x, r.y, r.w, r.h);
    int cx = r.x + r.w / 2;
    char hm[8], ampm[4], date[16];
    if (clockValid()) {
        strlcpy(hm, Time.format(Time.now(), "%l:%M").c_str(), sizeof(hm));
        strlcpy(ampm, Time.format(Time.now(), "%p").c_str(), sizeof(ampm));
        formatDate(date, sizeof(date));
    } else {
        strcpy(hm, "--:--");
        ampm[0] = 0;
        strcpy(date, "no time yet");
    }
    char *t = hm;
    while (*t == ' ') t++;  // %l pads single-digit hours
    // Large time with AM/PM beside it (aligned to its baseline), centered as one unit; the date below.
    int16_t bx, by;
    uint16_t tw, th, aw = 0, ah;
    canvas.setStyle(4);
    canvas.getTextBounds(t, 0, 0, &bx, &by, &tw, &th);
    if (ampm[0]) {
        canvas.setStyle(2);
        canvas.getTextBounds(ampm, 0, 0, &bx, &by, &aw, &ah);
        aw += 5;
    }
    const int timeH = 25, dateH = 13, gap = 12;  // cap heights of the 18pt and 9pt fonts
    int x0 = cx - (tw + aw) / 2;
    int ty = y + (h - timeH - gap - dateH) / 2;
    canvas.setStyle(4);
    canvas.setTextColor(theme.text);
    canvas.cursor(x0, ty);
    canvas.print(t);
    if (aw) {
        canvas.setStyle(2);
        canvas.setTextColor(theme.text2);
        canvas.cursor(x0 + tw + 5, ty + timeH - 13);
        canvas.print(ampm);
    }
    canvas.setStyle(2);
    canvas.setTextColor(theme.text2);
    printCentered(date, cx, ty + timeH + gap);
}

// Timer/stopwatch in place of the clock: label, big digits, and the time of day underneath.
static void drawTimerTile(Rect r) {
    int y = r.y, h = r.h;
    bool flash = timerDone() && (millis() / 500) % 2;
    canvas.fillRect(r.x, r.y, r.w, r.h, theme.separator);
    canvas.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, CARD_RADIUS, flash ? theme.critical : theme.surface);
    int cx = r.x + r.w / 2;
    const char *label = timerDone() ? "TIME'S UP" : timerMode() == TM_TIMER ? (timerRunning() ? "TIMER" : "TIMER PAUSED")
                                    : (timerRunning() ? "STOPWATCH" : "STOPWATCH PAUSED");
    char digits[12], now[12];
    formatTimer(digits, sizeof(digits));
    const int timeH = 25, top = y + (h - 9 - 8 - timeH - 10 - 13) / 2;
    canvas.setStyle(1);
    canvas.setTextColor(theme.text2);
    printCentered(label, cx, top);
    canvas.setStyle(4);
    canvas.setTextColor(theme.text);
    printCentered(digits, cx, top + 17);
    if (clockValid()) {
        strlcpy(now, Time.format(Time.now(), "%l:%M %p").c_str(), sizeof(now));
        char *t = now;
        while (*t == ' ') t++;
        canvas.setStyle(2);
        canvas.setTextColor(theme.text2);
        printCentered(t, cx, top + 17 + timeH + 10);
    }
}

static void drawClock() {
    Rect r = clockRect();
    renderRegion(r.x, r.y, r.w, r.h, [&] {
        if (timerMode() != TM_NONE) drawTimerTile(r);
        else drawClockTile(r);
    });
}

static void drawTable() {
    TableGeom t = tableGeom();
    renderRegion(0, t.y, TABLE_W, TABLE_HEADER_H, [&] { drawTableHeader(t.y); });
    for (int i = 0; i < t.rows; i++) {
        int ry = t.y + TABLE_HEADER_H + i * t.rowH;
        renderRegion(0, ry, TABLE_W, t.rowH, [&] { drawTableRow(i, ry, t.rowH); });
    }
    int end = t.y + TABLE_HEADER_H + t.rows * t.rowH;
    if (end < tableBottom()) renderRegion(0, end, TABLE_W, tableBottom() - end, [] { tableCard(); });
    drawClock();
}

// ---- LCARS layout: elbow frame + sidebar, time/date, CPU/GPU and RAM gauges, optional photo ----
struct LcarsGeom {
    int W, H, side, bar;
    Rect timeBox, photo, cpuBox, ramBox;
};

static LcarsGeom lcarsGeom() {
    LcarsGeom g;
    g.W = tft.width();
    g.H = tft.height();
    g.bar = 24;
    const bool photo = settings.lcarsPhoto;
    if (g.H > g.W) {  // portrait: content x 76..312
        g.side = 64;
        g.timeBox = {76, 32, 236, 96};
        g.photo = photo ? Rect{76, 128, 236, 148} : Rect{0, 0, 0, 0};
        g.cpuBox = photo ? Rect{76, 276, 236, 86} : Rect{76, 128, 236, 160};
        g.ramBox = photo ? Rect{76, 362, 236, 86} : Rect{76, 288, 236, 160};
    } else {  // landscape: content x 108..472
        g.side = 96;
        g.timeBox = photo ? Rect{108, 32, 210, 104} : Rect{108, 32, 364, 104};
        g.photo = photo ? Rect{318, 32, 154, 104} : Rect{0, 0, 0, 0};
        g.cpuBox = {108, 136, 364, 76};
        g.ramBox = {108, 212, 364, 76};
    }
    return g;
}

static bool lcarsActive() {
    return settings.view == VIEW_DASHBOARD && settings.layout == LAYOUT_LCARS;
}

// "Stardate" for fun: year.day-of-year, e.g. 2026.275.
static void formatStardate(char *buf, size_t len) {
    static const int CUM[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int y = Time.year(), m = Time.month(), d = Time.day();
    int doy = CUM[m - 1] + d + ((m > 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) ? 1 : 0);
    snprintf(buf, len, "STARDATE %d.%03d", y, doy);
}

// The frame: top and bottom elbows, the sidebar blocks with small readouts, bar segments and the title.
static void drawLcarsFrame(const LcarsGeom &g) {
    const int W = g.W, H = g.H, S = g.side, B = g.bar, E = 64;  // E = elbow height
    const uint16_t black = theme.surface;
    canvas.fillRect(0, 0, W, H, black);
    for (int k = 0; k < 2; k++) {  // k = 0 top elbow, 1 bottom elbow (mirrored vertically)
        uint16_t c = theme.accent;
        int y0 = k ? H - E : 0;
        int barY = k ? H - B : 0;
        canvas.fillRect(32, y0, S - 32, E, c);                    // core
        canvas.fillRect(0, k ? y0 : 32, S, E - 32, c);             // vertical arm
        canvas.fillCircle(32, k ? H - 33 : 32, 32, c);             // rounded outer corner
        canvas.fillRect(S, barY, 40, B, c);                         // horizontal arm
        // Concave inner corner where the arm meets the sidebar.
        int cy = k ? H - B - 12 : B + 12;
        canvas.fillRect(S, k ? H - B - 12 : B, 12, 12, c);
        canvas.fillCircle(S + 12, cy, 12, black);
        // Bar segments after the arm, a short accent segment, then a black gap holding the text, and an end cap.
        const char *text = k ? (stale ? (samples ? "NO HOST DATA" : "WAITING") : "LINK ACTIVE") : "SYSTEM STATUS";
        canvas.setStyle(2);
        int16_t bx, by;
        uint16_t tw, th;
        canvas.getTextBounds(text, 0, 0, &bx, &by, &tw, &th);
        int textX = W - 16 - tw;                 // text sits between textX and W - 16
        int seg2 = textX - 8 - 22;               // short accent segment just before the text gap
        int x = S + 44;
        canvas.fillRect(x, barY, seg2 - 4 - x, B, k ? theme.text : theme.button);
        canvas.fillRect(seg2, barY, 22, B, theme.accent);
        canvas.fillRoundRect(W - 10, barY, 10, B, 5, theme.accent);
        canvas.setTextColor(k ? (stale ? theme.critical : theme.text) : theme.accent);
        canvas.cursor(textX, barY + (B - 13) / 2);
        canvas.print(text);
    }
    // Sidebar blocks between the elbows, each with a small readout in black.
    const int top = E + 4, bottom = H - E - 4, n = 4, gap = 4;
    const int bh = (bottom - top - gap * (n - 1)) / n;
    const uint16_t colors[4] = {theme.button, theme.text, theme.accent, theme.button};
    char labels[4][20];
    snprintf(labels[0], 20, "DISK %.0f%%", metrics.disk);
    float net = metrics.netRx + metrics.netTx;
    if (net < 1000) snprintf(labels[1], 20, "NET %.0fK", net);
    else snprintf(labels[1], 20, "NET %.1fM", net / 1000);
    snprintf(labels[2], 20, "IO %.1fM", metrics.diskRead + metrics.diskWrite);
    snprintf(labels[3], 20, "%.8s", metrics.numProcs ? metrics.procs[0].name : "--");
    for (char *c = labels[3]; *c; c++) *c = toupper(*c);
    for (int i = 0; i < n; i++) {
        int by = top + i * (bh + gap);
        canvas.fillRect(0, by, S, bh, colors[i]);
        canvas.setStyle(1);
        canvas.setTextColor(black);
        printRight(labels[i], S - 4, by + bh - 11);
        char code[8];
        snprintf(code, sizeof(code), "%02d-%d", i + 1, 100 + i * 47);
        printRight(code, S - 4, by + 3);
    }
}

// Pill-shaped gauge: track in the grid color, filled to frac in color.
static void lcarsBar(int x, int y, int w, int h, float frac, uint16_t color) {
    canvas.fillRoundRect(x, y, w, h, h / 2, theme.grid);
    int fw = (int)(constrain(frac, 0.0f, 1.0f) * w);
    if (fw > 0) canvas.fillRoundRect(x, y, max(fw, h), h, h / 2, color);
}

static void drawLcarsTime(const LcarsGeom &g) {
    Rect r = g.timeBox;
    canvas.fillRect(r.x, r.y, r.w, r.h, theme.surface);
    char big[12], ampm[4] = "", date[20], star[24];
    const char *label = NULL;
    if (timerMode() != TM_NONE) {
        formatTimer(big, sizeof(big));
        label = timerDone() ? "TIME'S UP" : timerMode() == TM_TIMER ? "TIMER" : "STOPWATCH";
    } else if (clockValid()) {
        strlcpy(big, Time.format(Time.now(), "%l:%M").c_str(), sizeof(big));
        strlcpy(ampm, Time.format(Time.now(), "%p").c_str(), sizeof(ampm));
    } else {
        strcpy(big, "--:--");
    }
    char *t = big;
    while (*t == ' ') t++;
    if (clockValid()) {
        formatDate(date, sizeof(date));
        for (char *c = date; *c; c++) *c = toupper(*c);
        formatStardate(star, sizeof(star));
    } else {
        strcpy(date, "AWAITING TIME");
        star[0] = 0;
    }
    bool flash = timerDone() && (millis() / 500) % 2;
    int16_t bx, by;
    uint16_t tw, th;
    canvas.setStyle(4);
    canvas.getTextBounds(t, 0, 0, &bx, &by, &tw, &th);
    int ty = r.y + (label ? 22 : 12);
    if (label) {
        canvas.setStyle(1);
        canvas.setTextColor(flash ? theme.critical : theme.text2);
        canvas.cursor(r.x + 4, r.y + 8);
        canvas.print(label);
    }
    canvas.setStyle(4);
    canvas.setTextColor(flash ? theme.critical : theme.accent);
    canvas.cursor(r.x + 4, ty);
    canvas.print(t);
    if (ampm[0]) {
        canvas.setStyle(2);
        canvas.setTextColor(theme.accent);
        canvas.cursor(r.x + 4 + tw + 5, ty + 12);
        canvas.print(ampm);
    }
    canvas.setStyle(2);
    canvas.setTextColor(theme.text2);
    if (r.w >= 300) {  // wide: date and stardate on the right
        printRight(date, r.x + r.w - 4, r.y + 14);
        canvas.setStyle(1);
        canvas.setTextColor(theme.text);
        printRight(star, r.x + r.w - 4, r.y + 38);
    } else {
        canvas.cursor(r.x + 4, ty + 38);
        canvas.print(date);
        canvas.setStyle(1);
        canvas.setTextColor(theme.text);
        canvas.cursor(r.x + 4, ty + 60);
        canvas.print(star);
    }
    // A thin divider under the box, LCARS style.
    canvas.fillRoundRect(r.x, r.y + r.h - 6, r.w, 3, 1, theme.button);
}

// Metric box: title + value header, one or two labeled gauges, and a history graph if there is room.
static void drawLcarsMetric(Rect r, const Panel &p, const char *title, const char *value, int gauges,
                            const char *const labels[2], const float values[2]) {
    canvas.fillRect(r.x, r.y, r.w, r.h, theme.surface);
    canvas.setStyle(2);
    canvas.setTextColor(theme.text2);
    canvas.cursor(r.x + 4, r.y + 6);
    canvas.print(title);
    canvas.setTextColor(theme.text);
    printRight(value, r.x + r.w - 4, r.y + 6);
    int y = r.y + 28;
    for (int i = 0; i < gauges; i++) {
        canvas.setStyle(1);
        canvas.setTextColor(theme.text2);
        canvas.cursor(r.x + 4, y + 2);
        canvas.print(labels[i]);
        char pct[8];
        snprintf(pct, sizeof(pct), "%.0f%%", values[i]);
        canvas.setTextColor(theme.text);
        printRight(pct, r.x + r.w - 4, y + 2);
        lcarsBar(r.x + 32, y, r.w - 32 - 36, 12, values[i] / 100.0f, theme.series[i]);
        y += 18;
    }
    int gh = r.y + r.h - 8 - (y + 4);
    if (gh >= 14) drawGraph(p, r.x + 7, y + 7, r.w - 14, gh - 6, r.w > 300 ? 180 : 113, false);
}

static void drawLcarsCpu(const LcarsGeom &g) {
    static const char *const L[2] = {"CPU", "GPU"};
    float v[2] = {metrics.cpu, metrics.gpu};
    char value[20];
    snprintf(value, sizeof(value), "%.0f%% / %.0f%%", metrics.cpu, metrics.gpu);
    drawLcarsMetric(g.cpuBox, cpuPanel, "CPU / GPU", value, 2, L, v);
}

static void drawLcarsRam(const LcarsGeom &g) {
    static const char *const L[2] = {"RAM", ""};
    float v[2] = {metrics.ram, 0};
    char value[24];
    snprintf(value, sizeof(value), "%.1f / %.0f GB", metrics.ramUsed, metrics.ramTotal);
    drawLcarsMetric(g.ramBox, ramPanel, "MEMORY", value, 1, L, v);
}

static void lcarsRegion(Rect r, std::function<void()> f) {
    if (r.w > 0) renderRegion(r.x, r.y, r.w, r.h, f);
}

// Everything except the photo (which is fetched separately, so samples don't reload it).
static void drawLcars() {
    LcarsGeom g = lcarsGeom();
    const int W = g.W, H = g.H, S = g.side, B = g.bar;
    // Frame regions: left strip (sidebar + gap), top and bottom bars, right margin.
    lcarsRegion({0, 0, S + 12, H}, [&] { drawLcarsFrame(g); });
    lcarsRegion({S + 12, 0, W - S - 12, B + 8}, [&] { drawLcarsFrame(g); });
    lcarsRegion({S + 12, H - B - 8, W - S - 12, B + 8}, [&] { drawLcarsFrame(g); });
    lcarsRegion({W - 8, B + 8, 8, H - 2 * B - 16}, [&] { drawLcarsFrame(g); });
    lcarsRegion(g.timeBox, [&] { drawLcarsTime(g); });
    lcarsRegion(g.cpuBox, [&] { drawLcarsCpu(g); });
    lcarsRegion(g.ramBox, [&] { drawLcarsRam(g); });
}

static void drawLcarsTimeOnly() {
    LcarsGeom g = lcarsGeom();
    lcarsRegion(g.timeBox, [&] { drawLcarsTime(g); });
}

// ---- Tron layout: Flynn's terminal (Tron: Legacy) — a "top" window and a shell window ----
struct TronGeom {
    Rect top, term;
};

static TronGeom tronGeom() {
    if (portrait()) return {{0, 0, 320, 262}, {0, 266, 320, 214}};
    return {{0, 0, 288, 320}, {292, 0, 188, 320}};
}

static bool tronActive() {
    return settings.view == VIEW_DASHBOARD && settings.layout == LAYOUT_TRON;
}

// Blocky monospace pixel text (the native font, scaled) with a soft glow: the text drawn offset in the glow color
// first, then on top in the main color.
static void tronText(int x, int y, const char *text, int size, uint16_t color) {
    canvas.setStyle(1);
    canvas.setTextSize(size);
    canvas.setTextColor(theme.button);
    static const int8_t OFF[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (auto &o : OFF) {
        canvas.cursor(x + o[0], y + o[1]);
        canvas.print(text);
    }
    canvas.setTextColor(color);
    canvas.cursor(x, y);
    canvas.print(text);
    canvas.setTextSize(1);
}

// Terminal window chrome: pale title bar, menu row, border, scrollbar, and faint diagonal "glass" streaks.
static void tronWindow(Rect r, const char *title, int thumbFrom) {
    canvas.fillRect(r.x, r.y, r.w, r.h, theme.surface);
    for (int i = -2; i < 8; i++) {
        int x0 = r.x + i * 70;
        canvas.drawLine(x0, r.y + r.h, x0 + 140, r.y + 26, theme.grid);
        canvas.drawLine(x0 + 1, r.y + r.h, x0 + 141, r.y + 26, theme.grid);
    }
    canvas.drawRect(r.x, r.y, r.w, r.h, theme.accent);
    canvas.fillRect(r.x, r.y, r.w, 12, theme.accent);
    canvas.setStyle(1);
    canvas.setTextColor(theme.button);
    printCentered(title, r.x + r.w / 2, r.y + 2);
    canvas.setTextColor(theme.text2);
    canvas.cursor(r.x + 6, r.y + 15);
    printFit("File  Edit  View  Terminal  Tabs  Help", r.w - 12);
    canvas.drawFastHLine(r.x, r.y + 25, r.w, theme.button);
    int sx = r.x + r.w - 9, sy = r.y + 27, sh = r.h - 29;
    canvas.drawRect(sx, sy, 8, sh, theme.accent);
    canvas.fillRect(sx + 2, sy + thumbFrom * sh / 100, 4, sh - thumbFrom * sh / 100 - 2, theme.accent);
}

static void drawTronTop(const TronGeom &g) {
    Rect r = g.top;
    tronWindow(r, "top - micromonitor", 70);
    const int x = r.x + 6, w = r.w - 18;
    int y = r.y + 30;
    char line[64];
    char now[12] = "--:--:--";
    if (clockValid()) strlcpy(now, Time.format(Time.now(), "%H:%M:%S").c_str(), sizeof(now));
    snprintf(line, sizeof(line), "top - %s  cpu %.1f%%  gpu %.1f%%", now, metrics.cpu, metrics.gpu);
    tronText(x, y, line, 1, theme.text);
    snprintf(line, sizeof(line), "Mem: %.1fG used, %.1fG free, %.0fG total", metrics.ramUsed,
             max(0.0f, metrics.ramTotal - metrics.ramUsed), metrics.ramTotal);
    tronText(x, y += 10, line, 1, theme.text);
    snprintf(line, sizeof(line), "Dsk: rd %.1f wr %.1f MB/s, %.0f%% used", metrics.diskRead, metrics.diskWrite, metrics.disk);
    tronText(x, y += 10, line, 1, theme.text);
    snprintf(line, sizeof(line), "Net: rx %.0f tx %.0f KB/s", metrics.netRx, metrics.netTx);
    tronText(x, y += 10, line, 1, theme.text);
    // Inverted column header, like top's highlighted header row.
    y += 14;
    canvas.fillRect(x - 2, y - 2, w + 2, 19, theme.text2);
    canvas.setStyle(1);
    canvas.setTextSize(2);
    canvas.setTextColor(theme.surface);
    canvas.cursor(x, y);
    canvas.print("%CPU %MEM COMMAND");
    canvas.setTextSize(1);
    y += 20;
    const int rows = portrait() ? 5 : 5;
    for (int i = 0; i < rows && i < metrics.numProcs; i++) {
        const Proc &p = metrics.procs[i];
        char name[16];
        int maxChars = (w - 12 * 11) / 12;  // what fits after the two number columns
        snprintf(name, sizeof(name), "%.*s", max(4, min(maxChars, 15)), p.name);
        snprintf(line, sizeof(line), "%4.0f %4.1f %s", p.cpu, p.mem, name);
        tronText(x, y + i * 18, line, 2, theme.text);
    }
    // CPU history as glowing block bars along the bottom.
    int by = y + rows * 18 + 6, bh = r.y + r.h - 6 - by;
    if (bh >= 12) {
        tronText(x, by, "cpu", 1, theme.text2);
        int bx0 = x + 24, n = (w - 26) / 3;
        for (int i = 0; i < n && i < cpuPanel.count; i++) {
            float v = constrain(histAt(cpuPanel, 0, i), 0.0f, 100.0f);
            int hgt = max(1, (int)(v / 100.0f * bh));
            int bx = bx0 + (n - 1 - i) * 3;
            canvas.fillRect(bx, by + bh - hgt, 2, hgt, theme.series[0]);
        }
    }
}

static void drawTronTerm(const TronGeom &g) {
    Rect r = g.term;
    tronWindow(r, "Terminal", 85);
    const int x = r.x + 6, lh = portrait() ? 18 : 22;
    int y = r.y + 32;
    char line[40];
    auto out = [&](const char *text, uint16_t color) {
        tronText(x, y, text, 2, color);
        y += lh;
    };
    if (timerMode() != TM_NONE) {
        char digits[12];
        formatTimer(digits, sizeof(digits));
        bool flash = timerDone() && (millis() / 500) % 2;
        out(timerMode() == TM_TIMER ? "$ timer" : "$ stopwatch", theme.text2);
        if (timerDone()) out("TIME'S UP", flash ? theme.critical : theme.text);
        else {
            snprintf(line, sizeof(line), "%s%s", digits, timerRunning() ? "" : " (p)");
            out(line, theme.text);
        }
    } else {
        out("$ whoami", theme.text2);
        out("flynn", theme.text);
    }
    out("$ date", theme.text2);
    if (clockValid()) {
        char hm[12], date[16];
        strlcpy(hm, Time.format(Time.now(), "%l:%M %p").c_str(), sizeof(hm));
        formatDate(date, sizeof(date));
        char *t = hm;
        while (*t == ' ') t++;
        if (portrait()) {
            snprintf(line, sizeof(line), "%s %s", t, date);
            out(line, theme.text);
        } else {
            out(t, theme.text);
            out(date, theme.text);
        }
    } else {
        out("no time yet", theme.text);
    }
    out("$ grid -s", theme.text2);
    snprintf(line, sizeof(line), "CPU %.0f%% GPU %.0f%%", metrics.cpu, metrics.gpu);
    out(line, theme.text);
    snprintf(line, sizeof(line), "MEM %.0f%%", metrics.ram);
    out(line, theme.text);
    out(stale ? (samples ? "LINK LOST" : "WAITING") : "LINK ACTIVE", stale ? theme.critical : theme.good);
    // Prompt with a block cursor.
    tronText(x, y, "#", 2, theme.text);
    canvas.fillRect(x + 16, y, 10, 14, theme.text);
}

static void drawTron() {
    TronGeom g = tronGeom();
    renderRegion(g.top.x, g.top.y, g.top.w, g.top.h, [&] { drawTronTop(g); });
    renderRegion(g.term.x, g.term.y, g.term.w, g.term.h, [&] { drawTronTerm(g); });
    // The gap between the windows.
    Rect gap = portrait() ? Rect{0, 262, 320, 4} : Rect{288, 0, 4, 320};
    renderRegion(gap.x, gap.y, gap.w, gap.h, [] { canvas.fillScreen(theme.surface); });
}

static void drawTronTermOnly() {
    TronGeom g = tronGeom();
    renderRegion(g.term.x, g.term.y, g.term.w, g.term.h, [&] { drawTronTerm(g); });
}

// ---- Layouts ----
// Focus layout geometry: the main panel, then a row of 3 mini tiles (tap one to focus it).
struct FocusGeom {
    int mainH, tileY, tileW, tileH;
};

static FocusGeom focusGeom() {
    return portrait() ? FocusGeom{150, 150, 107, 60} : FocusGeom{150, 150, 160, 60};
}

// The three metrics not in focus, in order.
static void focusOthers(int others[3]) {
    for (int i = 0, k = 0; i < 4; i++)
        if (i != settings.focus) others[k++] = i;
}

void drawDashboard() {
    const bool tall = portrait();
    const int W = tft.width();
    if (settings.layout == LAYOUT_LCARS) {
        drawLcars();
        return;
    }
    if (settings.layout == LAYOUT_TRON) {
        drawTron();
        return;
    }
    switch (settings.layout) {
    case LAYOUT_STACKED: {
        const int h = tall ? 64 : 58;
        for (int i = 0; i < 4; i++) {
            int y = i * h;
            renderRegion(0, y, W, h, [&] { drawStrip(panels[i], 0, y, W, h); });
        }
        break;
    }
    case LAYOUT_FOCUS: {
        FocusGeom g = focusGeom();
        const Panel &main = panels[settings.focus];
        renderRegion(0, 0, W, g.mainH, [&] { drawStandardPanel(main, 0, 0, W, g.mainH, HISTORY); });
        int others[3];
        focusOthers(others);
        for (int k = 0; k < 3; k++) {
            int x = k * g.tileW, w = k == 2 ? W - 2 * g.tileW : g.tileW;
            renderRegion(x, g.tileY, w, g.tileH, [&] { drawMiniTile(panels[others[k]], x, g.tileY, w, g.tileH); });
        }
        break;
    }
    case LAYOUT_TILES: {
        const int tw = W / 2, th = tall ? 110 : 105;
        for (int i = 0; i < 4; i++) {
            int x = (i % 2) * tw, y = (i / 2) * th;
            renderRegion(x, y, tw, th, [&] { drawTile(panels[i], x, y, tw, th); });
        }
        break;
    }
    default:  // LAYOUT_QUAD: 2x2 in landscape, 4 full-width panels in portrait
        for (int i = 0; i < 4; i++) {
            int x = tall ? 0 : (i % 2) * 240, y = tall ? i * 72 : (i / 2) * 105;
            int w = tall ? W : 240, h = tall ? 72 : 105;
            renderRegion(x, y, w, h, [&] { drawStandardPanel(panels[i], x, y, w, h, tall ? 150 : 113); });
        }
        break;
    }
    drawTable();
}

void drawDashboardStatus() {
    if (settings.layout == LAYOUT_LCARS) {
        drawLcars();  // the frame shows the link status; cheap enough to redraw whole (the photo is untouched)
        return;
    }
    if (settings.layout == LAYOUT_TRON) {
        drawTron();
        return;
    }
    int y = tableGeom().y;
    renderRegion(0, y, TABLE_W, TABLE_HEADER_H, [&] { drawTableHeader(y); });
    drawClock();
}

bool mixedTap(int x, int y);

bool dashboardTap(int x, int y) {
    if (settings.layout == LAYOUT_LCARS) return mixedTap(x, y);  // tap the photo for the next one
    FocusGeom g = focusGeom();
    if (settings.layout != LAYOUT_FOCUS || y < g.tileY || y >= g.tileY + g.tileH) return false;
    int others[3];
    focusOthers(others);
    settings.focus = others[min(x / g.tileW, 2)];
    saveSettings();
    Serial.printlnf("focus %d", settings.focus);
    drawDashboard();
    return true;
}

// ---- Mixed view: photo + compact dashboard ----
// Landscape: a 240x160 photo (left or right) beside CPU/GPU and RAM mini tiles; DISK and NET panels; a strip with
// the top two processes and a compact clock. Portrait: the photo across the top, the tiles and panels in pairs
// below it, then the strip and a one-line clock.
struct MixGeom {
    Rect photo, cpu, ram, disk, net, strip, clock;
    bool oneLineClock;
};

static MixGeom mixGeom() {
    if (portrait())
        return {{0, 0, 320, 213}, {0, 213, 160, 70}, {160, 213, 160, 70}, {0, 283, 160, 105}, {160, 283, 160, 105},
                {0, 388, 320, 53}, {0, 441, 320, 39}, true};
    int px = settings.mixedSide ? 240 : 0, tx = settings.mixedSide ? 0 : 240;
    return {{px, 0, 240, 160}, {tx, 0, 240, 80}, {tx, 80, 240, 80}, {0, 160, 240, 105}, {240, 160, 240, 105},
            {0, 265, 320, 55}, {320, 265, 160, 55}, false};
}

static int mixedIndex = 0;
static int mixedCount = -1;
static uint32_t mixedChange = 0;

// Round the photo's corners by painting the separator color outside a radius-r arc in each corner.
static void roundCorners(int x, int y, int w, int h, int r) {
    const int cx[4] = {x, x + w - r, x, x + w - r}, cy[4] = {y, y, y + h - r, y + h - r};
    for (int k = 0; k < 4; k++) {
        renderRegion(cx[k], cy[k], r, r, [&] {
            for (int j = 0; j < r; j++)
                for (int i = 0; i < r; i++) {
                    // Distance from the arc center, which sits at the inner corner of this r x r square.
                    float dx = (k & 1) ? i + 0.5f : r - i - 0.5f;
                    float dy = (k & 2) ? j + 0.5f : r - j - 0.5f;
                    if (dx * dx + dy * dy > r * r) canvas.drawPixel(cx[k] + i, cy[k] + j, theme.separator);
                }
        });
    }
}

static void photoMessage(Rect p, const char *text) {
    renderRegion(p.x, p.y, p.w, p.h, [&] {
        card(p.x, p.y, p.w, p.h);
        canvas.setStyle(1);
        canvas.setTextColor(theme.text2);
        printCentered(text, p.x + p.w / 2, p.y + p.h / 2 - 4);
    });
}

// The photo slot of the current view: the mixed view's photo, or the LCARS layout's optional photo.
static Rect photoSlot() {
    if (settings.view == VIEW_MIXED) return mixGeom().photo;
    if (lcarsActive() && settings.lcarsPhoto) return lcarsGeom().photo;
    return {0, 0, 0, 0};
}

static void drawMixedPhoto() {
    Rect p = photoSlot();
    if (p.w == 0) return;
    mixedChange = millis();
    if (stale) {
        photoMessage(p, "photos need \xE6Monitor");
        mixedCount = -1;
        return;
    }
    int count;
    // 2px gap like the cards, then round the corners to match them.
    bool ok = fetchPicture(settings.folder, mixedIndex, p.x + 2, p.y + 2, p.w - 4, p.h - 4, count);
    mixedCount = count;
    if (count == 0 || !ok) {
        photoMessage(p, count == 0 ? "no pictures yet" : "picture failed");
        return;
    }
    renderRegion(p.x, p.y, p.w, 2, [&] { canvas.fillScreen(theme.separator); });
    renderRegion(p.x, p.y + p.h - 2, p.w, 2, [&] { canvas.fillScreen(theme.separator); });
    renderRegion(p.x, p.y, 2, p.h, [&] { canvas.fillScreen(theme.separator); });
    renderRegion(p.x + p.w - 2, p.y, 2, p.h, [&] { canvas.fillScreen(theme.separator); });
    roundCorners(p.x + 2, p.y + 2, p.w - 4, p.h - 4, CARD_RADIUS);
}

static void drawMixedStrip(Rect r) {
    renderRegion(r.x, r.y, r.w, r.h, [&] {
        card(r.x, r.y, r.w, r.h);
        int y = r.y;
        canvas.setStyle(1);
        canvas.setTextColor(theme.text2);
        canvas.cursor(8, y + 6);
        canvas.print("TOP PROCESSES");
        canvas.fillCircle(96, y + 9, 3, stale ? theme.critical : theme.good);
        canvas.cursor(103, y + 6);
        canvas.print(stale ? (samples ? "NO HOST DATA" : "WAITING FOR HOST") : "LIVE");
        printRight("CPU%", COL_CPU_RIGHT, y + 6);
        printRight("MEM%", COL_MEM_RIGHT, y + 6);
        for (int i = 0; i < 2 && i < metrics.numProcs; i++) {
            const Proc &p = metrics.procs[i];
            int ty = y + 18 + i * 17;
            canvas.setStyle(2);
            canvas.setTextColor(theme.text);
            canvas.cursor(8, ty);
            printFit(p.name, COL_CPU_RIGHT - 60);
            char buf[12];
            snprintf(buf, sizeof(buf), "%.1f", p.cpu);
            printRight(buf, COL_CPU_RIGHT, ty);
            snprintf(buf, sizeof(buf), "%.1f", p.mem);
            printRight(buf, COL_MEM_RIGHT, ty);
        }
    });
}

// Compact clock (or timer): two lines beside the strip in landscape, one line under it in portrait.
static void drawMixedClock() {
    MixGeom g = mixGeom();
    Rect r = g.clock;
    renderRegion(r.x, r.y, r.w, r.h, [&] {
        card(r.x, r.y, r.w, r.h);
        char hm[12], date[16];
        if (timerMode() != TM_NONE) {
            formatTimer(hm, sizeof(hm));
            strcpy(date, timerDone() ? "TIME'S UP" : timerMode() == TM_TIMER ? "TIMER" : "STOPWATCH");
            if (timerDone() && (millis() / 500) % 2)
                canvas.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, CARD_RADIUS, theme.critical);
        } else if (clockValid()) {
            strlcpy(hm, Time.format(Time.now(), "%l:%M %p").c_str(), sizeof(hm));
            formatDate(date, sizeof(date));
        } else {
            strcpy(hm, "--:--");
            strcpy(date, "no time yet");
        }
        char *t = hm;
        while (*t == ' ') t++;
        int cx = r.x + r.w / 2;
        if (g.oneLineClock) {
            char line[32];
            snprintf(line, sizeof(line), "%s   %s", t, date);
            canvas.setStyle(2);
            canvas.setTextColor(theme.text);
            printCentered(line, cx, r.y + (r.h - 13) / 2);
            return;
        }
        canvas.setStyle(2);
        canvas.setTextColor(theme.text);
        printCentered(t, cx, r.y + 10);
        canvas.setStyle(1);
        canvas.setTextColor(theme.text2);
        printCentered(date, cx, r.y + 34);
    });
}

void drawMixedPanels() {
    MixGeom g = mixGeom();
    renderRegion(g.cpu.x, g.cpu.y, g.cpu.w, g.cpu.h, [&] { drawMiniTile(cpuPanel, g.cpu.x, g.cpu.y, g.cpu.w, g.cpu.h); });
    renderRegion(g.ram.x, g.ram.y, g.ram.w, g.ram.h, [&] { drawMiniTile(ramPanel, g.ram.x, g.ram.y, g.ram.w, g.ram.h); });
    renderRegion(g.disk.x, g.disk.y, g.disk.w, g.disk.h,
                 [&] { drawStandardPanel(diskPanel, g.disk.x, g.disk.y, g.disk.w, g.disk.h, 113); });
    renderRegion(g.net.x, g.net.y, g.net.w, g.net.h,
                 [&] { drawStandardPanel(netPanel, g.net.x, g.net.y, g.net.w, g.net.h, 113); });
    drawMixedStrip(g.strip);
    drawMixedClock();
}

void mixedShow() {
    drawMixedPanels();
    drawMixedPhoto();
}

void mixedStep(int delta) {
    if (photoSlot().w == 0) return;
    if (mixedCount > 0) mixedIndex = (mixedIndex + delta + mixedCount) % mixedCount;
    drawMixedPhoto();
}

static bool inRect(Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

bool timerAreaHit(int x, int y) {
    if (lcarsActive()) return inRect(lcarsGeom().timeBox, x, y);
    if (tronActive()) return inRect(tronGeom().term, x, y);
    if (settings.view == VIEW_DASHBOARD) return inRect(clockRect(), x, y);
    if (settings.view == VIEW_MIXED) return inRect(mixGeom().clock, x, y);
    return albumBadgeHit(x, y);
}

void drawTimerTick() {
    if (lcarsActive()) drawLcarsTimeOnly();
    else if (tronActive()) drawTronTermOnly();
    else if (settings.view == VIEW_DASHBOARD) drawClock();
    else if (settings.view == VIEW_MIXED) drawMixedClock();
    else drawClockBadge();
}

void mixedResetIndex() {
    mixedIndex = 0;
    mixedCount = -1;
}

void dashboardPhoto() {
    if (photoSlot().w > 0) drawMixedPhoto();
}

void mixedTick() {
    if (settings.view == VIEW_ALBUM || photoSlot().w == 0 || menuOpen || uiBusy) return;
    uint32_t interval = slideSeconds() * 1000;
    // A single picture never needs reloading; an empty or failed folder is retried every 3s.
    if (mixedCount == 1) return;
    if (millis() - mixedChange >= (mixedCount > 0 ? interval : 3000)) {
        if (mixedCount > 0) mixedIndex = (mixedIndex + 1) % mixedCount;
        drawMixedPhoto();
    }
}

// Tap on the photo = next picture; anywhere else opens the menu (returns false).
bool mixedTap(int x, int y) {
    if (!inRect(photoSlot(), x, y)) return false;
    if (mixedCount > 0) mixedIndex = (mixedIndex + 1) % mixedCount;
    drawMixedPhoto();
    return true;
}
