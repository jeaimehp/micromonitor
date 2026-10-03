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

// ---- XP layout: Windows XP (Luna) Task Manager, Performance tab, on the Bliss desktop with the taskbar ----
// The layout is a skin: its colors are the fixed Luna / Task Manager palette, so it stays legible with any theme.
// The whole screen is one scene (drawXpScene, absolute coordinates); any rectangle is redrawn by rendering the scene
// clipped to it, which lets the timer balloon overlap the window and disappear cleanly. With the layout photo on
// (the same setting as LCARS), the photo gets its own "My Pictures" window and Task Manager makes room for it.
struct XpGeom {
    int W, H;
    bool narrow;            // Task Manager window under 400px wide: shorter title, tabs and status text
    Rect win, taskbar, tray, balloon;
    Rect photoWin, photo;   // photo: the picture inside photoWin (w = 0 when the photo is off)
    Rect cpuGauge, cpuHist, memGauge, memHist;
    Rect disk, net, procs;  // procs: the top process (landscape), the top 3 (portrait) or none (w = 0)
};

static XpGeom xpGeom() {
    XpGeom g;
    g.W = tft.width();
    g.H = tft.height();
    const bool tall = g.H > g.W;
    g.taskbar = {0, g.H - 30, g.W, 30};
    g.tray = {g.W - 110, g.H - 30, 110, 30};
    g.balloon = {g.W - 206, g.H - 92, 200, 62};  // body 52 tall + the tail down to the taskbar
    // Without the photo, portrait leaves a strip of desktop (the Bliss hill) above the taskbar. With it, the photo
    // window sits above Task Manager (portrait) or beside it, with the desktop showing below it (landscape).
    const bool photo = settings.lcarsPhoto;
    if (tall) {
        g.photoWin = photo ? Rect{4, 4, 312, 158} : Rect{0, 0, 0, 0};
        g.win = photo ? Rect{4, 166, 312, 280} : Rect{4, 4, 312, 396};
    } else {
        g.photoWin = photo ? Rect{310, 4, 166, 148} : Rect{0, 0, 0, 0};
        g.win = photo ? Rect{4, 4, 302, 282} : Rect{6, 4, 468, 282};
    }
    g.photo = photo ? Rect{g.photoWin.x + 3, g.photoWin.y + 25, g.photoWin.w - 6, g.photoWin.h - 28} : Rect{0, 0, 0, 0};
    g.narrow = g.win.w < 400;
    const bool roomy = tall && !photo;  // tall rows and the process list
    const int px = g.win.x + 13, pw = g.win.w - 26, gaugeW = 74, gap = 6, rowH = roomy ? 92 : 70;
    int y = g.win.y + 64;  // tab page content
    g.cpuGauge = {px, y, gaugeW, rowH};
    g.cpuHist = {px + gaugeW + gap, y, pw - gaugeW - gap, rowH};
    y += rowH + 4;
    g.memGauge = {px, y, gaugeW, rowH};
    g.memHist = {px + gaugeW + gap, y, pw - gaugeW - gap, rowH};
    y += rowH + 4;
    if (g.narrow) {
        int half = (pw - gap) / 2;
        g.disk = {px, y, half, 44};
        g.net = {px + half + gap, y, pw - half - gap, 44};
        g.procs = roomy ? Rect{px, y + 48, pw, 62} : Rect{0, 0, 0, 0};
    } else {
        int third = (pw - 2 * gap) / 3;
        g.disk = {px, y, third, 44};
        g.net = {px + third + gap, y, third, 44};
        g.procs = {px + 2 * (third + gap), y, pw - 2 * (third + gap), 44};
    }
    return g;
}

static bool xpActive() {
    return settings.view == VIEW_DASHBOARD && settings.layout == LAYOUT_XP;
}

static const uint16_t XP_FACE = rgb(0xece9d8);          // window body ("button face")
static const uint16_t XP_PAGE = rgb(0xfcfcfe);          // tab page
static const uint16_t XP_TEXT = rgb(0x000000);
static const uint16_t XP_TEXT2 = rgb(0x4d4d4d);
static const uint16_t XP_FRAME = rgb(0x0a3fd0);         // window border
static const uint16_t XP_TAB_BORDER = rgb(0x919b9c);
static const uint16_t XP_GROUP_BORDER = rgb(0xd0d0bf);
static const uint16_t XP_GROUP_TEXT = rgb(0x0046d5);    // group box captions are blue in Luna
static const uint16_t XP_GOOD = rgb(0x2e9a2e);
static const uint16_t XP_CRITICAL = rgb(0xd6301f);
static const uint16_t TM_BLACK = rgb(0x000000);         // Task Manager graphs: green on black
static const uint16_t TM_GRID = rgb(0x008040);
static const uint16_t TM_LINE = rgb(0x00ff00);
static const uint16_t TM_DIM = rgb(0x00591f);           // unlit LED segments

static const uint32_t XP_TITLE[5] = {0x4a9bff, 0x0b64ef, 0x0058ee, 0x0055e5, 0x0047cc};
static const uint32_t XP_BLUE_BUTTON[3] = {0x7aaeff, 0x2667ee, 0x1a55d8};
static const uint32_t XP_RED_BUTTON[3] = {0xf3967a, 0xe0461d, 0xc03514};
static const uint32_t XP_TAB[3] = {0xffffff, 0xf4f3ee, 0xe3e2da};
static const uint32_t XP_TASKBAR[5] = {0x3c81f3, 0x2a6ae3, 0x245edb, 0x2156d0, 0x1941a5};
static const uint32_t XP_START[4] = {0x6fc25a, 0x3c9a3c, 0x2f8a2f, 0x3f9e3a};
static const uint32_t XP_TRAY[3] = {0x1fb5f6, 0x0e9be6, 0x0b7fd2};
static const uint32_t XP_SKY[4] = {0x1f61d1, 0x3f86e2, 0x7fb3ec, 0xc4ddf5};

// Color at row i of h of a vertical gradient through n evenly spaced stops.
static uint16_t xpGradColor(const uint32_t *stops, int n, int i, int h) {
    int t = h > 1 ? i * (n - 1) * 256 / (h - 1) : 0;
    int seg = min(t >> 8, n - 2), f = t - seg * 256;
    uint32_t a = stops[seg], b = stops[seg + 1], c = 0;
    for (int s = 0; s <= 16; s += 8) {
        int ca = (a >> s) & 0xff, cb = (b >> s) & 0xff;
        c |= (uint32_t)(ca + (cb - ca) * f / 256) << s;
    }
    return rgb(c);
}

// How far row i (0 = the edge row) of a corner of radius rad sits in from the side.
static int xpInset(int i, int rad) {
    if (i >= rad) return 0;
    float d = rad - i - 0.5f;
    return rad - (int)sqrtf(rad * rad - d * d);
}

// Gradient-filled shape: top corners of radius rTop, bottom corners rBottom; the left side is square if !roundLeft.
static void xpFill(int x, int y, int w, int h, const uint32_t *stops, int n, int rTop, int rBottom,
                   bool roundLeft = true) {
    for (int i = 0; i < h; i++) {
        int inset = max(xpInset(i, rTop), xpInset(h - 1 - i, rBottom));
        int left = roundLeft ? inset : 0;
        canvas.fillRect(x + left, y + i, w - left - inset, 1, xpGradColor(stops, n, i, h));
    }
}

// Bliss: blue sky with a few clouds over the green hill.
static void xpDesktop(const XpGeom &g) {
    const int W = g.W, H = g.taskbar.y, top = H - (g.H > g.W ? 44 : 50);
    xpFill(0, 0, W, H, XP_SKY, 4, 0, 0);
    const uint16_t cloud = rgb(0xf2f7fd);
    const int CLOUDS[][3] = {{W * 7 / 10, top - 8, 9}, {W * 7 / 10 + 12, top - 12, 11}, {W * 7 / 10 + 26, top - 7, 8},
                             {W / 6, top - 4, 6}, {W / 6 + 9, top - 7, 8}};
    for (auto &c : CLOUDS) canvas.fillCircle(c[0], c[1], c[2], cloud);
    canvas.fillRect(W * 7 / 10 - 4, top - 6, 34, 7, cloud);
    // The hill: a parabola cresting left of center, a lit rim, then darker bands of grass.
    static const uint16_t GRASS[5] = {rgb(0x9bdc5e), rgb(0x6cc04a), rgb(0x52ab33), rgb(0x3f9624), rgb(0x2f7d18)};
    for (int x = 0; x < W; x++) {
        float t = constrain((x - W * 0.4f) / (W * 0.6f), -1.0f, 1.0f);
        int crest = top + (int)((H - top) * 0.6f * t * t);
        int y = crest;
        for (int b = 0; b < 5 && y < H; b++) {
            int bh = b == 0 ? 2 : b == 4 ? H - y : 6 + b * 2;
            canvas.fillRect(x, y, 1, bh, GRASS[b]);
            y += bh;
        }
    }
}

// Little Task Manager icon: a monitor with a green graph.
static void xpTmIcon(int x, int y) {
    canvas.fillRect(x, y, 14, 12, rgb(0xd8e4f8));
    canvas.fillRect(x + 1, y + 1, 12, 10, TM_BLACK);
    canvas.drawLine(x + 2, y + 8, x + 5, y + 4, TM_LINE);
    canvas.drawLine(x + 5, y + 4, x + 8, y + 7, TM_LINE);
    canvas.drawLine(x + 8, y + 7, x + 11, y + 2, TM_LINE);
    canvas.fillRect(x + 4, y + 12, 6, 2, rgb(0x9fb4d8));
}

// Little picture icon: sky, sun and a green hill.
static void xpPictureIcon(int x, int y) {
    canvas.fillRect(x, y, 14, 12, rgb(0xffffff));
    canvas.fillRect(x + 1, y + 1, 12, 10, rgb(0x5aa0f0));
    canvas.fillCircle(x + 9, y + 4, 2, rgb(0xffd23c));
    canvas.fillRect(x + 1, y + 8, 12, 3, rgb(0x52ab33));
    canvas.fillRect(x + 1, y + 7, 6, 1, rgb(0x52ab33));
}

// Minimize / maximize / close caption button (paler on an inactive window).
static void xpCaptionButton(int x, int y, int kind, bool active) {
    static const uint32_t PALE_BLUE[3] = {0xc4d6f6, 0x99b2ea, 0x8aa5e4}, PALE_RED[3] = {0xf3c0ae, 0xe49a80, 0xd88468};
    const uint16_t white = rgb(0xffffff);
    const uint32_t *stops = kind == 2 ? (active ? XP_RED_BUTTON : PALE_RED) : (active ? XP_BLUE_BUTTON : PALE_BLUE);
    xpFill(x, y, 21, 21, stops, 3, 3, 3);
    canvas.drawRoundRect(x, y, 21, 21, 3, white);
    if (kind == 0) canvas.fillRect(x + 5, y + 13, 7, 3, white);
    else if (kind == 1) {
        canvas.drawRect(x + 5, y + 5, 11, 11, white);
        canvas.fillRect(x + 5, y + 5, 11, 3, white);
    } else {
        for (int k = 0; k < 2; k++) {
            canvas.drawLine(x + 6 + k, y + 6, x + 13 + k, y + 14, white);
            canvas.drawLine(x + 13 + k, y + 6, x + 6 + k, y + 14, white);
        }
    }
}

// Group box: etched rounded border with a blue caption breaking the top edge.
static void xpGroup(Rect r, const char *caption) {
    canvas.drawRoundRect(r.x, r.y + 4, r.w, r.h - 4, 3, XP_GROUP_BORDER);
    canvas.setStyle(1);
    canvas.fillRect(r.x + 5, r.y, strlen(caption) * 6 + 5, 9, XP_PAGE);
    canvas.setTextColor(XP_GROUP_TEXT);
    canvas.cursor(r.x + 8, r.y);
    canvas.print(caption);
}

// "CPU Usage" / "Mem Usage": a black box with a two-column LED bar lit from the bottom and the value under it.
static void xpGauge(Rect r, const char *caption, float pct, const char *label) {
    xpGroup(r, caption);
    Rect in = {r.x + 10, r.y + 13, r.w - 20, r.h - 19};
    canvas.fillRect(in.x, in.y, in.w, in.h, TM_BLACK);
    const int bottom = in.y + in.h - 13, rows = (bottom - in.y - 4) / 3, cx = in.x + in.w / 2;
    const int lit = (int)(constrain(pct, 0.0f, 100.0f) / 100.0f * rows + 0.5f);
    for (int i = 0; i < rows; i++) {
        int y = bottom - (i + 1) * 3 + 1;
        uint16_t c = i < lit ? TM_LINE : TM_DIM;
        canvas.fillRect(cx - 13, y, 12, 2, c);
        canvas.fillRect(cx + 1, y, 12, 2, c);
    }
    canvas.setStyle(1);
    canvas.setTextColor(TM_LINE);
    printCentered(label, cx, in.y + in.h - 10);
}

// History pane: green grid that scrolls with the data, series s of p as a green line, and a label.
static void xpHistory(Rect r, const Panel &p, int s, const char *label) {
    const int cell = 12, step = 2;
    canvas.fillRect(r.x, r.y, r.w, r.h, TM_BLACK);
    for (int y = r.y + r.h - 1; y > r.y; y -= cell) canvas.drawFastHLine(r.x, y, r.w, TM_GRID);
    for (int x = r.x + r.w - 1 - (p.count * step) % cell; x >= r.x; x -= cell) canvas.drawFastVLine(x, r.y, r.h, TM_GRID);
    int shown = min(r.w / step + 1, p.count), prevX = -1, prevY = 0;
    for (int i = shown - 1; i >= 0; i--) {
        float v = constrain(histAt(p, s, i), 0.0f, 100.0f);
        int px = r.x + r.w - 1 - i * step, py = r.y + r.h - 1 - (int)(v / 100.0f * (r.h - 2));
        if (prevX >= 0) canvas.drawLine(prevX, prevY, px, py, TM_LINE);
        prevX = px;
        prevY = py;
    }
    canvas.setStyle(1);
    canvas.setTextColor(TM_LINE);
    canvas.cursor(r.x + 3, r.y + 3);
    canvas.print(label);
}

// Totals-style box: up to 3 "key ... value" lines; a line with an empty value shows just the key (fitted).
static void xpTotals(Rect r, const char *caption, const char *const keys[3], const char *const vals[3]) {
    xpGroup(r, caption);
    canvas.setStyle(1);
    canvas.setTextColor(XP_TEXT);
    for (int i = 0; i < 3; i++) {
        int y = r.y + 13 + i * 10;
        canvas.cursor(r.x + 8, y);
        printFit(keys[i], vals[i][0] ? r.w / 2 : r.w - 16);
        if (vals[i][0]) printRight(vals[i], r.x + r.w - 8, y);
    }
}

// Portrait: the top 3 processes as a small Processes-tab list.
static void xpProcList(Rect r) {
    xpGroup(r, "Processes");
    canvas.setStyle(1);
    const int cpuRight = r.x + r.w - 56, memRight = r.x + r.w - 8;
    canvas.setTextColor(XP_TEXT2);
    canvas.cursor(r.x + 8, r.y + 13);
    canvas.print("Image Name");
    printRight("CPU", cpuRight, r.y + 13);
    printRight("Mem", memRight, r.y + 13);
    canvas.drawFastHLine(r.x + 6, r.y + 23, r.w - 12, XP_GROUP_BORDER);
    canvas.setTextColor(XP_TEXT);
    for (int i = 0; i < 3 && i < metrics.numProcs; i++) {
        const Proc &p = metrics.procs[i];
        int y = r.y + 27 + i * 10;
        char buf[12];
        canvas.cursor(r.x + 8, y);
        printFit(p.name, cpuRight - 40 - r.x - 8);
        snprintf(buf, sizeof(buf), "%.1f %%", p.cpu);
        printRight(buf, cpuRight, y);
        snprintf(buf, sizeof(buf), "%.1f %%", p.mem);
        printRight(buf, memRight, y);
    }
}

// Window chrome: title bar (rounded top corners; pale when inactive), icon, caption, buttons, frame and body.
static void xpFrame(Rect w, const char *title, bool active, void (*icon)(int, int)) {
    static const uint32_t INACTIVE[3] = {0xa5bff0, 0x7d9ae2, 0x7390dc};
    if (active) xpFill(w.x, w.y, w.w, 25, XP_TITLE, 5, 7, 0);
    else xpFill(w.x, w.y, w.w, 25, INACTIVE, 3, 7, 0);
    const uint16_t frame = active ? XP_FRAME : rgb(0x7390dc);
    canvas.fillRect(w.x, w.y + 25, 3, w.h - 25, frame);
    canvas.fillRect(w.x + w.w - 3, w.y + 25, 3, w.h - 25, frame);
    canvas.fillRect(w.x, w.y + w.h - 3, w.w, 3, frame);
    canvas.fillRect(w.x + 3, w.y + 25, w.w - 6, w.h - 28, XP_FACE);
    icon(w.x + 7, w.y + 5);
    const int buttons = w.w < 200 ? 1 : 3;  // a small window keeps just the close button, for the caption's sake
    const int minX = w.x + w.w - 26 - (buttons - 1) * 23, textW = minX - 8 - (w.x + 27);
    canvas.setStyle(2);
    if (active) {
        canvas.setTextColor(rgb(0x0a246a));
        canvas.cursor(w.x + 27, w.y + 7);
        printFit(title, textW);
    }
    canvas.setTextColor(active ? rgb(0xffffff) : rgb(0xdfe8f8));
    canvas.cursor(w.x + 26, w.y + 6);
    printFit(title, textW);
    for (int k = 0; k < buttons; k++) xpCaptionButton(minX + k * 23, w.y + 2, k + 3 - buttons, active);
}

// The photo window; the picture itself is fetched into g.photo separately (see drawMixedPhoto).
static void xpPhotoWindow(const XpGeom &g) {
    if (g.photo.w > 0) xpFrame(g.photoWin, "My Pictures", false, xpPictureIcon);
}

static void xpWindow(const XpGeom &g) {
    const Rect w = g.win;
    const bool tall = g.narrow;  // the compact text variants
    xpFrame(w, tall ? "Task Manager" : "Windows Task Manager", true, xpTmIcon);
    // Menu row.
    canvas.setStyle(1);
    canvas.setTextColor(XP_TEXT);
    canvas.cursor(w.x + 9, w.y + 29);
    canvas.print("File  Options  View  Shut Down  Help");
    // Tab page with the Performance tab selected.
    const int statusY = w.y + w.h - 19;
    const Rect page = {w.x + 7, w.y + 58, w.w - 14, statusY - 2 - (w.y + 58)};
    canvas.fillRect(page.x, page.y, page.w, page.h, XP_PAGE);
    canvas.drawRect(page.x, page.y, page.w, page.h, XP_TAB_BORDER);
    static const char *const TABS[4] = {"Applications", "Processes", "Performance", "Networking"};
    const int pad = w.w < 310 ? 6 : tall ? 8 : 14;
    for (int i = 0, tx = page.x + 2; i < 4; i++) {
        const bool sel = i == 2;
        const int tw = strlen(TABS[i]) * 6 + pad, ty = sel ? w.y + 39 : w.y + 41, th = sel ? 20 : 17;
        if (sel) {
            canvas.fillRect(tx - 2, ty, tw + 4, th, XP_PAGE);
            canvas.fillRect(tx - 1, ty, tw + 2, 1, rgb(0xe68b2c));
            canvas.fillRect(tx - 2, ty + 1, tw + 4, 2, rgb(0xffc73c));
            canvas.drawFastVLine(tx - 2, ty + 3, th - 3, XP_TAB_BORDER);
            canvas.drawFastVLine(tx + tw + 1, ty + 3, th - 3, XP_TAB_BORDER);
        } else {
            xpFill(tx, ty, tw, th, XP_TAB, 3, 2, 0);
            canvas.drawFastHLine(tx + 2, ty, tw - 4, XP_TAB_BORDER);
            canvas.drawFastVLine(tx, ty + 2, th - 2, XP_TAB_BORDER);
            canvas.drawFastVLine(tx + tw - 1, ty + 2, th - 2, XP_TAB_BORDER);
        }
        canvas.setTextColor(XP_TEXT);
        canvas.cursor(tx + pad / 2, ty + (sel ? 7 : 5));
        canvas.print(TABS[i]);
        tx += tw;
    }
    // Performance tab contents.
    char cpuLabel[12], memLabel[12], buf[32];
    snprintf(cpuLabel, sizeof(cpuLabel), "%.0f %%", metrics.cpu);
    snprintf(memLabel, sizeof(memLabel), metrics.ramUsed < 100 ? "%.1f GB" : "%.0f GB", metrics.ramUsed);
    xpGauge(g.cpuGauge, "CPU Usage", metrics.cpu, cpuLabel);
    xpGauge(g.memGauge, "Mem Usage", metrics.ram, memLabel);
    Rect h = g.cpuHist;
    xpGroup(h, "CPU Usage History");
    const int paneW = (h.w - 12 - 4) / 2;
    snprintf(buf, sizeof(buf), "CPU %.0f%%", metrics.cpu);
    xpHistory({h.x + 6, h.y + 13, paneW, h.h - 19}, cpuPanel, 0, buf);
    snprintf(buf, sizeof(buf), "GPU %.0f%%", metrics.gpu);
    xpHistory({h.x + 10 + paneW, h.y + 13, h.w - 16 - paneW, h.h - 19}, cpuPanel, 1, buf);
    h = g.memHist;
    xpGroup(h, "Memory Usage History");
    snprintf(buf, sizeof(buf), "RAM %.0f%%", metrics.ram);
    xpHistory({h.x + 6, h.y + 13, h.w - 12, h.h - 19}, ramPanel, 0, buf);
    char v[3][16];
    snprintf(v[0], 16, "%.1f MB/s", metrics.diskRead);
    snprintf(v[1], 16, "%.1f MB/s", metrics.diskWrite);
    snprintf(v[2], 16, "%.0f %%", metrics.disk);
    static const char *const DISK[3] = {"Read", "Write", "Used"};
    const char *const dv[3] = {v[0], v[1], v[2]};
    xpTotals(g.disk, "Disk", DISK, dv);
    formatRate(v[0], 16, metrics.netRx);
    formatRate(v[1], 16, metrics.netTx);
    formatRate(v[2], 16, metrics.netRx + metrics.netTx);
    static const char *const NET[3] = {"Received", "Sent", "Total"};
    xpTotals(g.net, "Network", NET, dv);
    if (g.procs.w == 0) {
    } else if (g.H > g.W) xpProcList(g.procs);
    else {
        const bool any = metrics.numProcs > 0;
        snprintf(v[1], 16, "%.1f %%", any ? metrics.procs[0].cpu : 0.0f);
        snprintf(v[2], 16, "%.1f %%", any ? metrics.procs[0].mem : 0.0f);
        const char *const keys[3] = {any ? metrics.procs[0].name : "-", "CPU", "Mem"};
        const char *const vals[3] = {"", v[1], v[2]};
        xpTotals(g.procs, "Top Process", keys, vals);
    }
    // Status bar: link | CPU usage | commit charge, in sunken sections.
    canvas.setStyle(1);
    const int secs[3] = {w.x + 3, w.x + 95, w.x + 193};
    for (int k = 1; k < 3; k++) {
        canvas.drawFastVLine(secs[k] - 3, statusY + 3, 12, rgb(0xaca899));
        canvas.drawFastVLine(secs[k] - 2, statusY + 3, 12, rgb(0xffffff));
    }
    canvas.fillCircle(secs[0] + 8, statusY + 8, 3, stale ? XP_CRITICAL : XP_GOOD);
    canvas.setTextColor(XP_TEXT);
    canvas.cursor(secs[0] + 15, statusY + 5);
    canvas.print(stale ? (samples ? "No host data" : "Waiting") : "Connected");
    snprintf(buf, sizeof(buf), "CPU Usage: %.0f%%", metrics.cpu);
    canvas.cursor(secs[1] + 4, statusY + 5);
    canvas.print(buf);
    snprintf(buf, sizeof(buf), tall ? "Mem: %.1fG/%.0fG" : "Commit Charge: %.1fG / %.0fG", metrics.ramUsed,
             metrics.ramTotal);
    canvas.cursor(secs[2] + 4, statusY + 5);
    canvas.print(buf);
}

// Taskbar: start button, the Task Manager task button (pressed), and the tray with the link icon and the clock
// (or the timer/stopwatch).
static void xpTaskbar(const XpGeom &g) {
    const Rect t = g.taskbar, tr = g.tray;
    const bool tall = g.H > g.W;
    xpFill(t.x, t.y, t.w, t.h, XP_TASKBAR, 5, 0, 0);
    const int startW = tall ? 92 : 98;
    xpFill(0, t.y, startW, t.h, XP_START, 4, 12, 12, false);
    // Windows flag: four tiles.
    static const uint16_t FLAG[4] = {rgb(0xf25022), rgb(0x7fba00), rgb(0x00a4ef), rgb(0xffb900)};
    for (int k = 0; k < 4; k++) canvas.fillRect(9 + (k % 2) * 8, t.y + 7 + (k / 2) * 8, 7, 7, FLAG[k]);
    canvas.setStyle(3);
    canvas.setTextColor(rgb(0x1e5a1e));
    canvas.cursor(29, t.y + 8);
    canvas.print("start");
    canvas.setTextColor(rgb(0xffffff));
    canvas.cursor(28, t.y + 7);
    canvas.print("start");
    // Task button.
    const int bx = startW + 6, bw = tr.x - 6 - bx > 150 ? 150 : tr.x - 6 - bx;
    canvas.fillRoundRect(bx, t.y + 3, bw, t.h - 6, 3, rgb(0x1e4fb5));
    canvas.drawRoundRect(bx, t.y + 3, bw, t.h - 6, 3, rgb(0x163f99));
    canvas.drawFastHLine(bx + 2, t.y + 4, bw - 4, rgb(0x163f99));
    xpTmIcon(bx + 6, t.y + 8);
    canvas.setStyle(1);
    canvas.setTextColor(rgb(0xffffff));
    canvas.cursor(bx + 26, t.y + 11);
    printFit("Task Manager", bw - 30);
    // Tray.
    const bool flash = timerDone() && (millis() / 500) % 2;
    if (flash) canvas.fillRect(tr.x, tr.y, tr.w, tr.h, XP_CRITICAL);
    else xpFill(tr.x, tr.y, tr.w, tr.h, XP_TRAY, 3, 0, 0);
    canvas.drawFastVLine(tr.x, tr.y, tr.h, rgb(0x0f5fb9));
    canvas.drawFastVLine(tr.x + 1, tr.y, tr.h, rgb(0x5cc8fa));
    // Network icon: two monitors; a red X when the host link is down.
    const int ix = tr.x + 8, iy = t.y + 9;
    for (int k = 0; k < 2; k++) {
        int mx = ix + k * 5, my = iy + (1 - k) * 4;
        canvas.fillRect(mx, my, 8, 7, rgb(0xffffff));
        canvas.fillRect(mx + 1, my + 1, 6, 4, rgb(0x2a7de1));
        canvas.fillRect(mx + 3, my + 7, 2, 2, rgb(0xffffff));
    }
    if (stale) {
        canvas.fillCircle(ix + 11, iy + 10, 4, XP_CRITICAL);
        canvas.drawLine(ix + 9, iy + 8, ix + 13, iy + 12, rgb(0xffffff));
        canvas.drawLine(ix + 13, iy + 8, ix + 9, iy + 12, rgb(0xffffff));
    }
    char text[12];
    if (timerMode() != TM_NONE) formatTimer(text, sizeof(text));
    else if (clockValid()) strlcpy(text, Time.format(Time.now(), "%l:%M %p").c_str(), sizeof(text));
    else strcpy(text, "--:--");
    char *s = text;
    while (*s == ' ') s++;
    canvas.setStyle(2);
    canvas.setTextColor(timerMode() != TM_NONE && !timerRunning() && !timerDone() ? rgb(0xbcd9f5) : rgb(0xffffff));
    printRight(s, tr.x + tr.w - 7, t.y + 9);
}

// Notification balloon over the tray when a timer finishes (tap it, or the tray, to dismiss).
static void xpBalloon(const XpGeom &g) {
    const Rect b = g.balloon;
    const uint16_t fill = rgb(0xffffe1), edge = rgb(0x000000);
    const int bh = 52, tipX = g.tray.x + g.tray.w - 40;
    canvas.fillRoundRect(b.x, b.y, b.w, bh, 7, fill);
    canvas.drawRoundRect(b.x, b.y, b.w, bh, 7, edge);
    canvas.fillTriangle(tipX - 14, b.y + bh - 1, tipX, b.y + bh - 1, tipX, b.y + b.h - 1, fill);
    canvas.drawLine(tipX - 14, b.y + bh - 1, tipX, b.y + b.h - 1, edge);
    canvas.drawLine(tipX, b.y + bh - 1, tipX, b.y + b.h - 1, edge);
    canvas.fillCircle(b.x + 14, b.y + 14, 7, rgb(0x2a6fdc));
    canvas.setStyle(1);
    canvas.setTextColor(rgb(0xffffff));
    canvas.cursor(b.x + 12, b.y + 10);
    canvas.print("i");
    canvas.setStyle(2);
    canvas.setTextColor(XP_TEXT);
    canvas.cursor(b.x + 28, b.y + 8);
    canvas.print("Timer");
    canvas.drawRect(b.x + b.w - 17, b.y + 5, 12, 12, rgb(0xaca899));
    canvas.drawLine(b.x + b.w - 14, b.y + 8, b.x + b.w - 9, b.y + 13, XP_TEXT2);
    canvas.drawLine(b.x + b.w - 9, b.y + 8, b.x + b.w - 14, b.y + 13, XP_TEXT2);
    canvas.setStyle(1);
    canvas.setTextColor(XP_TEXT);
    canvas.cursor(b.x + 10, b.y + 28);
    canvas.print("Time's up!");
    canvas.cursor(b.x + 10, b.y + 39);
    canvas.print("Tap here to dismiss.");
}

static void drawXpScene(const XpGeom &g) {
    xpDesktop(g);
    xpPhotoWindow(g);
    xpWindow(g);
    xpTaskbar(g);
    if (timerDone()) xpBalloon(g);
}

// Everything except the photo (fetched separately, so samples don't reload it): the screen in up to four
// rectangles around it.
static void drawXp() {
    XpGeom g = xpGeom();
    const Rect p = g.photo;
    auto scene = [&] { drawXpScene(g); };
    if (p.w == 0) {
        renderRegion(0, 0, g.W, g.H, scene);
        return;
    }
    if (p.y > 0) renderRegion(0, 0, g.W, p.y, scene);
    renderRegion(0, p.y, p.x, p.h, scene);
    if (p.x + p.w < g.W) renderRegion(p.x + p.w, p.y, g.W - p.x - p.w, p.h, scene);
    renderRegion(0, p.y + p.h, g.W, g.H - p.y - p.h, scene);
}

// The tray and the balloon area above it (so a dismissed balloon is painted over).
static void drawXpTray() {
    XpGeom g = xpGeom();
    Rect b = g.balloon;
    renderRegion(b.x, b.y, g.W - b.x, g.H - b.y, [&] { drawXpScene(g); });
}

// ---- System 7 layout: the classic Mac desktop ----
// Menu bar (rainbow Apple, menus, clock or timer), a dithered desktop, and black-and-white windows: "About This
// Macintosh" (memory + the top apps with bars, active), "CPU Meter" (CPU and GPU history), "Macintosh HD" (disk and
// network as Finder icons) and, with the layout photo on, a "Photo" window. A finished timer shows an alert with an
// OK button. Like XP, a fixed palette, one scene in absolute coordinates, rendered around the photo.
struct S7Geom {
    int W, H;
    Rect about, meter, finder, photoWin, photo, alert, clock;
};

static S7Geom s7Geom() {
    S7Geom g;
    g.W = tft.width();
    g.H = tft.height();
    const bool photo = settings.lcarsPhoto, tall = g.H > g.W;
    g.photoWin = g.photo = {0, 0, 0, 0};
    if (tall) {
        if (photo) {
            g.photoWin = {6, 28, 308, 132};
            g.meter = {6, 168, 308, 90};
            g.about = {6, 266, 308, 120};
            g.finder = {6, 394, 307, 80};
        } else {
            g.meter = {6, 28, 308, 130};
            g.about = {6, 166, 308, 196};
            g.finder = {6, 370, 308, 104};
        }
        g.alert = {30, 190, 260, 100};
    } else {
        g.meter = {296, 28, 178, 140};
        if (photo) {
            g.about = {6, 28, 282, 170};
            g.finder = {6, 206, 282, 106};
            g.photoWin = {296, 176, 178, 136};
        } else {
            g.about = {6, 28, 282, 284};
            g.finder = {296, 176, 178, 136};
        }
        g.alert = {110, 110, 260, 100};
    }
    if (photo) g.photo = {g.photoWin.x + 1, g.photoWin.y + 20, g.photoWin.w - 2, g.photoWin.h - 21};
    g.clock = {g.W - 112, 0, 112, 20};
    return g;
}

static bool s7Active() {
    return settings.view == VIEW_DASHBOARD && settings.layout == LAYOUT_SYSTEM7;
}

static const uint16_t S7_WHITE = rgb(0xffffff);
static const uint16_t S7_BLACK = rgb(0x000000);
static const uint16_t S7_GREY = rgb(0x808080);
static const uint16_t S7_LIGHT = rgb(0xc8c8c8);
static const uint16_t S7_DESK0 = rgb(0x5a5aa0);  // desktop dither
static const uint16_t S7_DESK1 = rgb(0x7878bc);

// Content area of a window (inside the frame, under the title bar).
static Rect s7Content(Rect r) {
    return {r.x + 1, r.y + 20, r.w - 2, r.h - 21};
}

// Window: drop shadow, frame, title bar. Active: pinstripes, close and zoom boxes, black title; inactive: plain, grey.
static void s7Window(Rect r, const char *title, bool active) {
    canvas.fillRect(r.x + 1, r.y + r.h, r.w, 1, S7_BLACK);
    canvas.fillRect(r.x + r.w, r.y + 1, 1, r.h, S7_BLACK);
    canvas.fillRect(r.x, r.y, r.w, r.h, S7_WHITE);
    canvas.drawRect(r.x, r.y, r.w, r.h, S7_BLACK);
    canvas.drawFastHLine(r.x, r.y + 19, r.w, S7_BLACK);
    canvas.setStyle(2);
    int16_t bx, by;
    uint16_t tw, th;
    canvas.getTextBounds(title, 0, 0, &bx, &by, &tw, &th);
    const int cx = r.x + r.w / 2;
    if (active) {
        for (int i = 0; i < 6; i++) canvas.drawFastHLine(r.x + 2, r.y + 4 + i * 2, r.w - 4, S7_BLACK);
        const int boxY = r.y + 4, closeX = r.x + 9, zoomX = r.x + r.w - 20;
        canvas.fillRect(closeX - 1, boxY - 1, 13, 13, S7_WHITE);
        canvas.drawRect(closeX, boxY, 11, 11, S7_BLACK);
        canvas.fillRect(zoomX - 1, boxY - 1, 13, 13, S7_WHITE);
        canvas.drawRect(zoomX, boxY, 11, 11, S7_BLACK);
        canvas.drawRect(zoomX, boxY, 7, 7, S7_BLACK);
        canvas.fillRect(cx - tw / 2 - 6, r.y + 2, tw + 12, 16, S7_WHITE);
    }
    canvas.setTextColor(active ? S7_BLACK : S7_GREY);
    printCentered(title, cx, r.y + 3);
}

// Native pixel font, emboldened by printing twice one pixel apart (close to Chicago at this size).
static void s7Bold(int x, int y, const char *text, uint16_t color = S7_BLACK) {
    canvas.setStyle(1);
    canvas.setTextColor(color);
    for (int k = 0; k < 2; k++) {
        canvas.cursor(x + k, y);
        canvas.print(text);
    }
}

static void s7Text(int x, int y, const char *text, uint16_t color = S7_BLACK) {
    canvas.setStyle(1);
    canvas.setTextColor(color);
    canvas.cursor(x, y);
    canvas.print(text);
}

// The six-stripe Apple, 12x13.
static void s7Apple(int x, int y) {
    static const char *const ROWS[13] = {
        ".......##...", "......##....", "..###..###..", ".##########.", "##########..", "#########...",
        "#########...", "#########...", "##########..", "###########.", ".##########.", ".#########..",
        "..###..###..",
    };
    static const uint16_t BANDS[13] = {rgb(0x61bb46), rgb(0x61bb46), rgb(0x61bb46), rgb(0x61bb46), rgb(0xfdb827),
                                       rgb(0xfdb827), rgb(0xf5821f), rgb(0xf5821f), rgb(0xe03a3e), rgb(0xe03a3e),
                                       rgb(0x963d97), rgb(0x963d97), rgb(0x009ddc)};
    for (int j = 0; j < 13; j++)
        for (int i = 0; i < 12; i++)
            if (ROWS[j][i] == '#') canvas.drawPixel(x + i, y + j, BANDS[j]);
}

// Compact Macintosh icon (18x24): case, screen, floppy slot, foot.
static void s7MacIcon(int x, int y) {
    canvas.fillRoundRect(x, y, 18, 22, 2, rgb(0xeeeadc));
    canvas.drawRoundRect(x, y, 18, 22, 2, S7_BLACK);
    canvas.fillRect(x + 3, y + 3, 12, 9, S7_LIGHT);
    canvas.drawRect(x + 3, y + 3, 12, 9, S7_BLACK);
    canvas.drawFastHLine(x + 9, y + 16, 6, S7_BLACK);
    canvas.fillRect(x + 1, y + 22, 16, 2, S7_BLACK);
}

// Small icons for the About list: an application diamond, and a memory chip.
static void s7AppIcon(int x, int y) {
    for (int i = 0; i <= 5; i++) {
        canvas.drawFastHLine(x + 5 - i, y + i, 2 * i + 1, i == 5 ? S7_BLACK : S7_WHITE);
        canvas.drawFastHLine(x + 5 - i, y + 10 - i, 2 * i + 1, S7_WHITE);
    }
    canvas.drawLine(x, y + 5, x + 5, y, S7_BLACK);
    canvas.drawLine(x + 5, y, x + 10, y + 5, S7_BLACK);
    canvas.drawLine(x + 10, y + 5, x + 5, y + 10, S7_BLACK);
    canvas.drawLine(x + 5, y + 10, x, y + 5, S7_BLACK);
    canvas.fillRect(x + 4, y + 4, 3, 3, S7_BLACK);
}

static void s7ChipIcon(int x, int y) {
    canvas.fillRect(x + 1, y + 2, 10, 7, S7_BLACK);
    for (int i = 0; i < 5; i++) {
        canvas.drawPixel(x + 2 + i * 2, y, S7_BLACK);
        canvas.drawPixel(x + 2 + i * 2, y + 1, S7_BLACK);
        canvas.drawPixel(x + 2 + i * 2, y + 9, S7_BLACK);
        canvas.drawPixel(x + 2 + i * 2, y + 10, S7_BLACK);
    }
}

// About-box bar: outlined, filled black to frac.
static void s7Bar(int x, int y, int w, int h, float frac) {
    canvas.fillRect(x, y, w, h, S7_WHITE);
    canvas.drawRect(x, y, w, h, S7_BLACK);
    int fw = (int)(constrain(frac, 0.0f, 1.0f) * (w - 2));
    if (fw > 0) canvas.fillRect(x + 1, y + 1, fw, h - 2, S7_BLACK);
}

static void s7MenuBar(const S7Geom &g) {
    canvas.fillRect(0, 0, g.W, 20, S7_WHITE);
    canvas.drawFastHLine(0, 19, g.W, S7_BLACK);
    s7Apple(14, 3);
    static const char *const MENUS[5] = {"File", "Edit", "View", "Label", "Special"};
    const int count = g.H > g.W ? 3 : 5;
    canvas.setStyle(2);
    canvas.setTextColor(S7_BLACK);
    int x = 42;
    for (int i = 0; i < count; i++) {
        int16_t bx, by;
        uint16_t tw, th;
        canvas.getTextBounds(MENUS[i], 0, 0, &bx, &by, &tw, &th);
        canvas.cursor(x, 4);
        canvas.print(MENUS[i]);
        x += tw + 16;
    }
    // Clock (or the timer), then the application menu's Finder icon at the right end.
    char text[12];
    if (timerMode() != TM_NONE) formatTimer(text, sizeof(text));
    else if (clockValid()) strlcpy(text, Time.format(Time.now(), "%l:%M %p").c_str(), sizeof(text));
    else strcpy(text, "--:--");
    char *s = text;
    while (*s == ' ') s++;
    canvas.setTextColor(timerMode() != TM_NONE && !timerRunning() && !timerDone() ? S7_GREY : S7_BLACK);
    printRight(s, g.W - 34, 4);
    const int ix = g.W - 26;
    canvas.fillRect(ix, 3, 14, 14, rgb(0x9fb8ff));
    canvas.fillRect(ix + 7, 3, 7, 14, rgb(0xd8e2ff));
    canvas.drawRect(ix, 3, 14, 14, S7_BLACK);
    canvas.drawFastVLine(ix + 7, 4, 9, S7_BLACK);
    canvas.drawPixel(ix + 4, 7, S7_BLACK);
    canvas.drawPixel(ix + 10, 7, S7_BLACK);
    canvas.drawFastHLine(ix + 4, 12, 7, S7_BLACK);
}

// Rounded screen corners, as on the classic Mac's display.
static void s7ScreenCorners(const S7Geom &g) {
    const int r = 7;
    for (int j = 0; j < r; j++) {
        int in = xpInset(j, r);
        canvas.fillRect(0, j, in, 1, S7_BLACK);
        canvas.fillRect(g.W - in, j, in, 1, S7_BLACK);
        canvas.fillRect(0, g.H - 1 - j, in, 1, S7_BLACK);
        canvas.fillRect(g.W - in, g.H - 1 - j, in, 1, S7_BLACK);
    }
}

static void s7About(const S7Geom &g) {
    s7Window(g.about, "About This Macintosh", true);
    const Rect c = s7Content(g.about);
    char buf[40];
    s7MacIcon(c.x + 10, c.y + 7);
    s7Bold(c.x + 38, c.y + 6, "System Software 7.1");
    snprintf(buf, sizeof(buf), "Total Memory: %.1f GB", metrics.ramTotal);
    s7Text(c.x + 38, c.y + 18, buf);
    snprintf(buf, sizeof(buf), "Largest Unused Block: %.1f GB", max(0.0f, metrics.ramTotal - metrics.ramUsed));
    s7Text(c.x + 38, c.y + 28, buf);
    int y = c.y + 40;
    canvas.drawFastHLine(c.x + 6, y, c.w - 12, S7_BLACK);
    canvas.drawFastHLine(c.x + 6, y + 2, c.w - 12, S7_BLACK);
    y += 6;
    // Rows: memory first, then the top apps by CPU. Bars on the right with the value after them.
    const int barW = c.w > 290 ? 110 : 90, barX = c.x + c.w - 44 - barW, rowH = c.h > 200 ? 18 : 14;
    const int rows = min(1 + metrics.numProcs, (c.y + c.h - 2 - y) / rowH);
    for (int i = 0; i < rows; i++) {
        int ry = y + i * rowH, ty = ry + (rowH - 7) / 2;
        float frac;
        if (i == 0) {
            s7ChipIcon(c.x + 8, ry + (rowH - 11) / 2);
            s7Bold(c.x + 26, ty, "Memory");
            snprintf(buf, sizeof(buf), "%.1fG", metrics.ramUsed);
            frac = metrics.ram / 100.0f;
        } else {
            const Proc &p = metrics.procs[i - 1];
            s7AppIcon(c.x + 8, ry + (rowH - 11) / 2);
            canvas.setStyle(1);
            canvas.setTextColor(S7_BLACK);
            canvas.cursor(c.x + 26, ty);
            printFit(p.name, barX - 8 - (c.x + 26));
            snprintf(buf, sizeof(buf), "%.1f%%", p.cpu);
            frac = p.cpu / 100.0f;
        }
        s7Bar(barX, ry + (rowH - 10) / 2, barW, 10, frac);
        canvas.setStyle(1);
        canvas.setTextColor(S7_BLACK);
        printRight(buf, c.x + c.w - 6, ty);
    }
}

// History pane: framed, dotted quarter lines, the newest samples as a filled area (solid or dithered).
static void s7History(Rect r, const Panel &p, int s, bool dither) {
    canvas.fillRect(r.x, r.y, r.w, r.h, S7_WHITE);
    for (int q = 1; q < 4; q++)
        for (int x = r.x + 1; x < r.x + r.w - 1; x += 3) canvas.drawPixel(x, r.y + r.h * q / 4, S7_GREY);
    const int inner = r.h - 2, shown = min(r.w - 2, p.count);
    for (int i = 0; i < shown; i++) {
        int hgt = (int)(constrain(histAt(p, s, i), 0.0f, 100.0f) / 100.0f * inner + 0.5f);
        if (hgt <= 0) continue;
        int x = r.x + r.w - 2 - i;
        if (dither) {
            canvas.fillPattern(x, r.y + r.h - 1 - hgt, 1, hgt, S7_BLACK, S7_WHITE);
            canvas.drawPixel(x, r.y + r.h - 1 - hgt, S7_BLACK);
        } else canvas.fillRect(x, r.y + r.h - 1 - hgt, 1, hgt, S7_BLACK);
    }
    canvas.drawRect(r.x, r.y, r.w, r.h, S7_BLACK);
}

static void s7Meter(const S7Geom &g) {
    s7Window(g.meter, "CPU Meter", false);
    const Rect c = s7Content(g.meter);
    const int paneH = (c.h - 6) / 2;
    char buf[16];
    for (int s = 0; s < 2; s++) {
        int y = c.y + 3 + s * paneH;
        s7Bold(c.x + 6, y + 1, s ? "GPU" : "CPU");
        snprintf(buf, sizeof(buf), "%.0f%%", s ? metrics.gpu : metrics.cpu);
        canvas.setStyle(1);
        canvas.setTextColor(S7_BLACK);
        printRight(buf, c.x + 42, y + 1);
        s7History({c.x + 48, y, c.w - 54, paneH - 3}, cpuPanel, s, s == 1);
    }
}

// Finder icons: a document (disk) and a folder (network).
static void s7DocIcon(int x, int y) {
    canvas.fillRect(x, y, 16, 20, S7_WHITE);
    canvas.drawFastHLine(x, y, 11, S7_BLACK);
    canvas.drawLine(x + 11, y, x + 15, y + 4, S7_BLACK);
    canvas.drawFastVLine(x + 15, y + 4, 16, S7_BLACK);
    canvas.drawFastHLine(x, y + 19, 16, S7_BLACK);
    canvas.drawFastVLine(x, y, 20, S7_BLACK);
    canvas.drawFastVLine(x + 11, y, 5, S7_BLACK);
    canvas.drawFastHLine(x + 11, y + 4, 5, S7_BLACK);
    for (int k = 0; k < 4; k++) canvas.drawFastHLine(x + 3, y + 8 + k * 3, 9, S7_GREY);
}

static void s7FolderIcon(int x, int y) {
    canvas.fillRect(x + 1, y + 1, 8, 3, rgb(0xb4c8ff));
    canvas.drawRect(x, y, 9, 4, S7_BLACK);
    canvas.fillRect(x, y + 3, 22, 15, rgb(0xb4c8ff));
    canvas.drawRect(x, y + 3, 22, 15, S7_BLACK);
    canvas.drawFastHLine(x + 1, y + 6, 20, S7_BLACK);
}

static void s7Finder(const S7Geom &g) {
    s7Window(g.finder, "Macintosh HD", false);
    const Rect c = s7Content(g.finder);
    char buf[32];
    snprintf(buf, sizeof(buf), "4 items");
    s7Text(c.x + 6, c.y + 3, buf);
    snprintf(buf, sizeof(buf), "%.0f%% used", metrics.disk);
    canvas.setStyle(1);
    printRight(buf, c.x + c.w - 6, c.y + 3);
    canvas.drawFastHLine(c.x, c.y + 12, c.w, S7_BLACK);
    canvas.drawFastHLine(c.x, c.y + 14, c.w, S7_BLACK);
    static const char *const NAMES[4] = {"Disk Read", "Disk Write", "Received", "Sent"};
    char vals[4][16];
    snprintf(vals[0], 16, "%.1f MB/s", metrics.diskRead);
    snprintf(vals[1], 16, "%.1f MB/s", metrics.diskWrite);
    formatRate(vals[2], 16, metrics.netRx);
    formatRate(vals[3], 16, metrics.netTx);
    const int cols = c.w >= 260 ? 4 : 2, rows = 4 / cols, top = c.y + 16;
    const int cellW = c.w / cols, cellH = (c.y + c.h - top) / rows;
    for (int i = 0; i < 4; i++) {
        int cx = c.x + (i % cols) * cellW + cellW / 2, cy = top + (i / cols) * cellH + (cellH - 41) / 2;
        if (i < 2) s7DocIcon(cx - 8, cy);
        else s7FolderIcon(cx - 11, cy + 2);
        canvas.setStyle(1);
        canvas.setTextColor(S7_BLACK);
        printCentered(NAMES[i], cx, cy + 23);
        printCentered(vals[i], cx, cy + 33);
    }
}

static void s7PhotoWindow(const S7Geom &g) {
    if (g.photo.w > 0) s7Window(g.photoWin, "Photo", false);
}

// Alert for a finished timer: double frame, caution icon, message and the default OK button.
static void s7Alert(const S7Geom &g) {
    const Rect a = g.alert;
    canvas.fillRect(a.x, a.y, a.w, a.h, S7_WHITE);
    canvas.drawRect(a.x, a.y, a.w, a.h, S7_BLACK);
    canvas.drawRect(a.x + 3, a.y + 3, a.w - 6, a.h - 6, S7_BLACK);
    canvas.drawRect(a.x + 4, a.y + 4, a.w - 8, a.h - 8, S7_BLACK);
    // Caution: a triangle with "!".
    const int ix = a.x + 16, iy = a.y + 16;
    canvas.fillTriangle(ix + 16, iy, ix, iy + 28, ix + 32, iy + 28, S7_BLACK);
    canvas.fillTriangle(ix + 16, iy + 5, ix + 4, iy + 26, ix + 28, iy + 26, rgb(0xffeb3b));
    canvas.fillRect(ix + 15, iy + 10, 3, 9, S7_BLACK);
    canvas.fillRect(ix + 15, iy + 21, 3, 3, S7_BLACK);
    canvas.setStyle(2);
    canvas.setTextColor(S7_BLACK);
    canvas.cursor(a.x + 62, a.y + 18);
    canvas.print("Time's up!");
    s7Text(a.x + 62, a.y + 40, timerMode() == TM_TIMER ? "Your timer has finished." : "");
    // OK: rounded button with the thick default outline.
    const int bw = 64, bh = 22, bx = a.x + a.w - bw - 16, by = a.y + a.h - bh - 14;
    for (int k = 0; k < 3; k++) canvas.drawRoundRect(bx - 4 + k, by - 4 + k, bw + 8 - 2 * k, bh + 8 - 2 * k, 9 - k, S7_BLACK);
    canvas.drawRoundRect(bx, by, bw, bh, 6, S7_BLACK);
    canvas.setStyle(2);
    printCentered("OK", bx + bw / 2, by + 5);
}

static void drawS7Scene(const S7Geom &g) {
    canvas.fillPattern(0, 20, g.W, g.H - 20, S7_DESK0, S7_DESK1);
    s7MenuBar(g);
    s7Meter(g);
    s7Finder(g);
    s7PhotoWindow(g);
    s7About(g);
    if (timerDone()) s7Alert(g);
    s7ScreenCorners(g);
}

// Everything except the photo, in up to four rectangles around it.
static void drawS7() {
    S7Geom g = s7Geom();
    const Rect p = g.photo;
    auto scene = [&] { drawS7Scene(g); };
    if (p.w == 0) {
        renderRegion(0, 0, g.W, g.H, scene);
        return;
    }
    if (p.y > 0) renderRegion(0, 0, g.W, p.y, scene);
    renderRegion(0, p.y, p.x, p.h, scene);
    if (p.x + p.w < g.W) renderRegion(p.x + p.w, p.y, g.W - p.x - p.w, p.h, scene);
    renderRegion(0, p.y + p.h, g.W, g.H - p.y - p.h, scene);
}

// Timer tick: the menu bar clock, and the alert while the timer is done. When the alert goes away the whole screen
// is redrawn (it may have covered the photo, which is fetched again).
static void drawS7Tick() {
    static bool alertShown = false;
    S7Geom g = s7Geom();
    auto scene = [&] { drawS7Scene(g); };
    renderRegion(g.clock.x, g.clock.y, g.clock.w, g.clock.h, scene);
    if (timerDone()) {
        alertShown = true;
        renderRegion(g.alert.x, g.alert.y, g.alert.w, g.alert.h, scene);
    } else if (alertShown) {
        alertShown = false;
        drawS7();
        dashboardPhoto();
    }
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
    if (settings.layout == LAYOUT_XP) {
        drawXp();
        return;
    }
    if (settings.layout == LAYOUT_SYSTEM7) {
        drawS7();
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
    if (settings.layout == LAYOUT_XP) {
        drawXp();
        return;
    }
    if (settings.layout == LAYOUT_SYSTEM7) {
        drawS7();
        return;
    }
    int y = tableGeom().y;
    renderRegion(0, y, TABLE_W, TABLE_HEADER_H, [&] { drawTableHeader(y); });
    drawClock();
}

bool mixedTap(int x, int y);

bool dashboardTap(int x, int y) {
    if (settings.layout == LAYOUT_LCARS || settings.layout == LAYOUT_XP || settings.layout == LAYOUT_SYSTEM7)
        return mixedTap(x, y);  // tap the photo for the next one
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
        if (xpActive() || s7Active()) canvas.fillRect(p.x, p.y, p.w, p.h, XP_PAGE);  // the window's content area
        else card(p.x, p.y, p.w, p.h);
        canvas.setStyle(1);
        canvas.setTextColor(xpActive() || s7Active() ? XP_TEXT2 : theme.text2);
        printCentered(text, p.x + p.w / 2, p.y + p.h / 2 - 4);
    });
}

// The photo slot of the current view: the mixed view's photo, or the LCARS layout's optional photo.
static Rect photoSlot() {
    if (settings.view == VIEW_MIXED) return mixGeom().photo;
    if (lcarsActive() && settings.lcarsPhoto) return lcarsGeom().photo;
    if (xpActive()) return xpGeom().photo;
    if (s7Active()) return s7Geom().photo;
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
    if (xpActive() || s7Active()) {  // XP / System 7: the picture fills the window's content; the frame is its border
        bool ok = fetchPicture(settings.folder, mixedIndex, p.x, p.y, p.w, p.h, count);
        mixedCount = count;
        if (count == 0 || !ok) photoMessage(p, count == 0 ? "no pictures yet" : "picture failed");
        return;
    }
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
    if (xpActive()) return inRect(xpGeom().tray, x, y) || (timerDone() && inRect(xpGeom().balloon, x, y));
    if (s7Active()) return inRect(s7Geom().clock, x, y) || (timerDone() && inRect(s7Geom().alert, x, y));
    if (settings.view == VIEW_DASHBOARD) return inRect(clockRect(), x, y);
    if (settings.view == VIEW_MIXED) return inRect(mixGeom().clock, x, y);
    return albumBadgeHit(x, y);
}

void drawTimerTick() {
    if (lcarsActive()) drawLcarsTimeOnly();
    else if (tronActive()) drawTronTermOnly();
    else if (xpActive()) drawXpTray();
    else if (s7Active()) drawS7Tick();
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
    if (photoHeld() && mixedCount > 0) return;  // held: stay on this picture
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
