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
    printRight(buf, x + w - 8, y + 25);
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

static TableGeom tableGeom() {
    if (settings.layout == LAYOUT_STACKED) return {232, 3, 24};
    return {210, 5, 19};
}

// The table shares the bottom band with the clock tile on the right.
static const int TABLE_W = 320;
static const int CLOCK_X = TABLE_W;
static const int CLOCK_W = SCREEN_W - TABLE_W;
static const int TABLE_HEADER_H = 15;
static const int COL_BAR_X = 146;       // CPU bar, scaled to one full core (100%), clipped
static const int COL_BAR_W = 56;
static const int COL_CPU_RIGHT = 258;   // right edge of the CPU% value
static const int COL_MEM_RIGHT = 310;   // right edge of the MEM% value

static void tableCard() {
    int y = tableGeom().y;
    card(0, y, TABLE_W, SCREEN_H - y);
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

// Clock tile: large time, AM/PM, and the date, in the bottom-right corner of the dashboard.
static void drawClockTile(int y) {
    int h = SCREEN_H - y;
    card(CLOCK_X, y, CLOCK_W, h);
    int cx = CLOCK_X + CLOCK_W / 2;
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
static void drawTimerTile(int y) {
    int h = SCREEN_H - y;
    bool flash = timerDone() && (millis() / 500) % 2;
    canvas.fillRect(CLOCK_X, y, CLOCK_W, h, theme.separator);
    canvas.fillRoundRect(CLOCK_X + 2, y + 2, CLOCK_W - 4, h - 4, CARD_RADIUS, flash ? theme.critical : theme.surface);
    int cx = CLOCK_X + CLOCK_W / 2;
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
    int y = tableGeom().y;
    renderRegion(CLOCK_X, y, CLOCK_W, SCREEN_H - y, [&] {
        if (timerMode() != TM_NONE) drawTimerTile(y);
        else drawClockTile(y);
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
    if (end < SCREEN_H) renderRegion(0, end, TABLE_W, SCREEN_H - end, [] { tableCard(); });
    drawClock();
}

// ---- Layouts ----
static const int FOCUS_MAIN_H = 150;
static const int FOCUS_TILE_Y = 150;
static const int FOCUS_TILE_H = 60;

// The three metrics not in focus, in order.
static void focusOthers(int others[3]) {
    for (int i = 0, k = 0; i < 4; i++)
        if (i != settings.focus) others[k++] = i;
}

void drawDashboard() {
    switch (settings.layout) {
    case LAYOUT_STACKED:
        for (int i = 0; i < 4; i++) {
            int y = i * 58;
            renderRegion(0, y, SCREEN_W, 58, [&] { drawStrip(panels[i], 0, y, SCREEN_W, 58); });
        }
        break;
    case LAYOUT_FOCUS: {
        const Panel &main = panels[settings.focus];
        renderRegion(0, 0, SCREEN_W, FOCUS_MAIN_H, [&] { drawStandardPanel(main, 0, 0, SCREEN_W, FOCUS_MAIN_H, HISTORY); });
        int others[3];
        focusOthers(others);
        for (int k = 0; k < 3; k++) {
            int x = k * 160;
            renderRegion(x, FOCUS_TILE_Y, 160, FOCUS_TILE_H,
                         [&] { drawMiniTile(panels[others[k]], x, FOCUS_TILE_Y, 160, FOCUS_TILE_H); });
        }
        break;
    }
    case LAYOUT_TILES:
        for (int i = 0; i < 4; i++) {
            int x = (i % 2) * 240, y = (i / 2) * 105;
            renderRegion(x, y, 240, 105, [&] { drawTile(panels[i], x, y, 240, 105); });
        }
        break;
    default:  // LAYOUT_QUAD
        for (int i = 0; i < 4; i++) {
            int x = (i % 2) * 240, y = (i / 2) * 105;
            renderRegion(x, y, 240, 105, [&] { drawStandardPanel(panels[i], x, y, 240, 105, 113); });
        }
        break;
    }
    drawTable();
}

void drawDashboardStatus() {
    int y = tableGeom().y;
    renderRegion(0, y, TABLE_W, TABLE_HEADER_H, [&] { drawTableHeader(y); });
    drawClock();
}

bool dashboardTap(int x, int y) {
    if (settings.layout != LAYOUT_FOCUS || y < FOCUS_TILE_Y || y >= FOCUS_TILE_Y + FOCUS_TILE_H) return false;
    int others[3];
    focusOthers(others);
    settings.focus = others[min(x / 160, 2)];
    saveSettings();
    Serial.printlnf("focus %d", settings.focus);
    drawDashboard();
    return true;
}

// ---- Mixed view: photo + compact dashboard ----
// Top half: a 240x160 photo (left or right) beside CPU/GPU and RAM mini tiles. Bottom: DISK and NET panels,
// then a strip with the top two processes and a compact clock.
static const int MIX_PHOTO_W = 240, MIX_PHOTO_H = 160;
static const int MIX_PANEL_Y = 160, MIX_PANEL_H = 105;
static const int MIX_STRIP_Y = MIX_PANEL_Y + MIX_PANEL_H;  // 265
static int mixedIndex = 0;
static int mixedCount = -1;
static uint32_t mixedChange = 0;

static int mixedPhotoX() {
    return settings.mixedSide ? 240 : 0;
}

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

static void drawMixedPhoto() {
    int x = mixedPhotoX();
    mixedChange = millis();
    if (stale) {
        renderRegion(x, 0, MIX_PHOTO_W, MIX_PHOTO_H, [&] {
            card(x, 0, MIX_PHOTO_W, MIX_PHOTO_H);
            canvas.setStyle(1);
            canvas.setTextColor(theme.text2);
            printCentered("photos need \xE6Monitor", x + MIX_PHOTO_W / 2, MIX_PHOTO_H / 2 - 4);
        });
        mixedCount = -1;
        return;
    }
    int count;
    // 2px gap like the cards, then round the corners to match them.
    bool ok = fetchPicture(settings.folder, mixedIndex, x + 2, 2, MIX_PHOTO_W - 4, MIX_PHOTO_H - 4, count);
    mixedCount = count;
    if (count == 0 || !ok) {
        renderRegion(x, 0, MIX_PHOTO_W, MIX_PHOTO_H, [&] {
            card(x, 0, MIX_PHOTO_W, MIX_PHOTO_H);
            canvas.setStyle(1);
            canvas.setTextColor(theme.text2);
            printCentered(count == 0 ? "no pictures yet" : "picture failed", x + MIX_PHOTO_W / 2, MIX_PHOTO_H / 2 - 4);
        });
        return;
    }
    renderRegion(x, 0, MIX_PHOTO_W, 2, [&] { canvas.fillScreen(theme.separator); });
    renderRegion(x, MIX_PHOTO_H - 2, MIX_PHOTO_W, 2, [&] { canvas.fillScreen(theme.separator); });
    renderRegion(x, 0, 2, MIX_PHOTO_H, [&] { canvas.fillScreen(theme.separator); });
    renderRegion(x + MIX_PHOTO_W - 2, 0, 2, MIX_PHOTO_H, [&] { canvas.fillScreen(theme.separator); });
    roundCorners(x + 2, 2, MIX_PHOTO_W - 4, MIX_PHOTO_H - 4, CARD_RADIUS);
}

static void drawMixedClock();

static void drawMixedStrip() {
    const int w = TABLE_W, h = SCREEN_H - MIX_STRIP_Y;
    renderRegion(0, MIX_STRIP_Y, w, h, [&] {
        card(0, MIX_STRIP_Y, w, h);
        int y = MIX_STRIP_Y;
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
    drawMixedClock();
}

// Compact clock (or timer) beside the mixed view's process strip.
static void drawMixedClock() {
    const int h = SCREEN_H - MIX_STRIP_Y;
    renderRegion(CLOCK_X, MIX_STRIP_Y, CLOCK_W, h, [&] {
        card(CLOCK_X, MIX_STRIP_Y, CLOCK_W, h);
        char hm[12], date[16];
        if (timerMode() != TM_NONE) {
            formatTimer(hm, sizeof(hm));
            strcpy(date, timerDone() ? "TIME'S UP" : timerMode() == TM_TIMER ? "TIMER" : "STOPWATCH");
            if (timerDone() && (millis() / 500) % 2)
                canvas.fillRoundRect(CLOCK_X + 2, MIX_STRIP_Y + 2, CLOCK_W - 4, h - 4, CARD_RADIUS, theme.critical);
        } else if (clockValid()) {
            strlcpy(hm, Time.format(Time.now(), "%l:%M %p").c_str(), sizeof(hm));
            formatDate(date, sizeof(date));
        } else {
            strcpy(hm, "--:--");
            strcpy(date, "no time yet");
        }
        char *t = hm;
        while (*t == ' ') t++;
        canvas.setStyle(2);
        canvas.setTextColor(theme.text);
        printCentered(t, CLOCK_X + CLOCK_W / 2, MIX_STRIP_Y + 10);
        canvas.setStyle(1);
        canvas.setTextColor(theme.text2);
        printCentered(date, CLOCK_X + CLOCK_W / 2, MIX_STRIP_Y + 34);
    });
}

void drawMixedPanels() {
    int tx = settings.mixedSide ? 0 : 240;
    renderRegion(tx, 0, 240, 80, [&] { drawMiniTile(cpuPanel, tx, 0, 240, 80); });
    renderRegion(tx, 80, 240, 80, [&] { drawMiniTile(ramPanel, tx, 80, 240, 80); });
    renderRegion(0, MIX_PANEL_Y, 240, MIX_PANEL_H,
                 [&] { drawStandardPanel(diskPanel, 0, MIX_PANEL_Y, 240, MIX_PANEL_H, 113); });
    renderRegion(240, MIX_PANEL_Y, 240, MIX_PANEL_H,
                 [&] { drawStandardPanel(netPanel, 240, MIX_PANEL_Y, 240, MIX_PANEL_H, 113); });
    drawMixedStrip();
}

void mixedShow() {
    drawMixedPanels();
    drawMixedPhoto();
}

void mixedStep(int delta) {
    if (mixedCount > 0) mixedIndex = (mixedIndex + delta + mixedCount) % mixedCount;
    drawMixedPhoto();
}

void drawTimerTick() {
    if (settings.view == VIEW_DASHBOARD) drawClock();
    else if (settings.view == VIEW_MIXED) drawMixedClock();
    else drawClockBadge();
}

void mixedResetIndex() {
    mixedIndex = 0;
    mixedCount = -1;
}

void mixedTick() {
    if (settings.view != VIEW_MIXED || menuOpen || uiBusy) return;
    uint32_t interval = (uint32_t)SLIDE_SECONDS[settings.slideIdx % NUM_SLIDE_OPTIONS] * 1000;
    // A single picture never needs reloading; an empty or failed folder is retried every 3s.
    if (mixedCount == 1) return;
    if (millis() - mixedChange >= (mixedCount > 0 ? interval : 3000)) {
        if (mixedCount > 0) mixedIndex = (mixedIndex + 1) % mixedCount;
        drawMixedPhoto();
    }
}

// Tap on the photo = next picture; anywhere else opens the menu (returns false).
bool mixedTap(int x, int y) {
    int px = mixedPhotoX();
    if (x < px || x >= px + MIX_PHOTO_W || y >= MIX_PHOTO_H) return false;
    if (mixedCount > 0) mixedIndex = (mixedIndex + 1) % mixedCount;
    drawMixedPhoto();
    return true;
}
