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
    {"CPU", 1, 100.0f},
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
    pushSample(cpuPanel, metrics.cpu);
    snprintf(cpuPanel.value, sizeof(cpuPanel.value), "%.1f%%", metrics.cpu);
    snprintf(cpuPanel.big, sizeof(cpuPanel.big), "%.0f%%", metrics.cpu);
    snprintf(cpuPanel.sub, sizeof(cpuPanel.sub), "top: %s", metrics.numProcs ? metrics.procs[0].name : "-");

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
    if (grid) {
        for (int g = 1; g <= 3; g++) {
            int gy = y + h * g / 4;
            for (int gx = x; gx < x + w; gx += 4) canvas.drawPixel(gx, gy, theme.grid);
        }
        canvas.drawFastHLine(x, y, w, theme.grid);
    }
    canvas.drawFastHLine(x, y + h, w, theme.grid);
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
    canvas.setTextSize(1);
    canvas.setTextColor(theme.text2);
    for (int s = 0; s < p.numSeries && p.numSeries == 2; s++) {
        canvas.fillRect(x, y, 8, 8, theme.series[s]);
        char buf[20];
        float v = p.count ? histAt(p, s, 0) : 0.0f;
        snprintf(buf, sizeof(buf), v < 10 ? "%s %.1f" : "%s %.0f", p.labels[s], v);
        canvas.setCursor(x + 12, y);
        canvas.print(buf);
        x += 12 + strlen(buf) * 6 + 10;
    }
    return x;
}

static void scaleText(const Panel &p, int n, char *buf, size_t len) {
    if (p.fixedMax > 0) buf[0] = 0;
    else snprintf(buf, len, "max %g %s", scaleMax(p, n), p.unit);
}

static void panelBackground(int x, int y, int w, int h) {
    canvas.fillRect(x, y, w, h, theme.surface);
    // 2px separators on the right and bottom edges keep the panels visually apart.
    canvas.fillRect(x + w - 2, y, 2, h, theme.separator);
    canvas.fillRect(x, y + h - 2, w, 2, theme.separator);
}

// Standard panel: title + headline value, legend row, graph (Quad panels and the Focus main panel).
static void drawStandardPanel(const Panel &p, int x, int y, int w, int h, int n) {
    panelBackground(x, y, w, h);
    canvas.setTextSize(2);
    canvas.setTextColor(theme.text2);
    canvas.setCursor(x + 8, y + 7);
    canvas.print(p.title);
    canvas.setTextColor(theme.text);
    printRight(p.value, x + w - 8, y + 7);
    drawLegend(p, x + 8, y + 25);
    char buf[24];
    scaleText(p, n, buf, sizeof(buf));
    canvas.setTextSize(1);
    canvas.setTextColor(theme.text2);
    printRight(buf, x + w - 8, y + 25);
    drawGraph(p, x + 8, y + 36, w - 16, h - 43, n, true);
}

// Stacked strip: label column on the left, long graph on the right.
static void drawStrip(const Panel &p, int x, int y, int w, int h) {
    panelBackground(x, y, w, h);
    canvas.setTextSize(2);
    canvas.setTextColor(theme.text2);
    canvas.setCursor(x + 8, y + 5);
    canvas.print(p.title);
    canvas.setTextSize(1);
    canvas.setTextColor(theme.text);
    canvas.setCursor(x + 8, y + 24);
    canvas.print(p.value);
    if (p.numSeries == 2) {
        // Two legend lines stacked in the label column.
        canvas.setTextColor(theme.text2);
        for (int s = 0; s < 2; s++) {
            canvas.fillRect(x + 8, y + 35 + s * 11, 8, 8, theme.series[s]);
            char buf[20];
            float v = p.count ? histAt(p, s, 0) : 0.0f;
            snprintf(buf, sizeof(buf), v < 10 ? "%s %.1f" : "%s %.0f", p.labels[s], v);
            canvas.setCursor(x + 20, y + 35 + s * 11);
            canvas.print(buf);
        }
    }
    const int gx = x + 120, gw = w - 128;
    drawGraph(p, gx, y + 4, gw, h - 10, HISTORY, true);
    char buf[24];
    scaleText(p, HISTORY, buf, sizeof(buf));
    if (buf[0]) {
        int bw = strlen(buf) * 6 + 4;
        canvas.fillRect(gx + 2, y + 5, bw, 10, theme.surface);
        canvas.setTextColor(theme.text2);
        canvas.setCursor(gx + 4, y + 6);
        canvas.print(buf);
    }
}

// Tile: title, large number, detail line, sparkline.
static void drawTile(const Panel &p, int x, int y, int w, int h) {
    panelBackground(x, y, w, h);
    canvas.setTextSize(2);
    canvas.setTextColor(theme.text2);
    canvas.setCursor(x + 8, y + 6);
    canvas.print(p.title);
    canvas.setTextSize(4);
    canvas.setTextColor(theme.text);
    canvas.setCursor(x + 8, y + 27);
    canvas.print(p.big);
    canvas.setTextSize(1);
    canvas.setTextColor(theme.text2);
    canvas.setCursor(x + 8, y + 63);
    canvas.print(p.sub);
    drawGraph(p, x + 8, y + 75, w - 16, h - 81, 113, false);
}

// Small Focus-layout tile: title + value + sparkline. Tapping it makes it the focused metric.
static void drawMiniTile(const Panel &p, int x, int y, int w, int h) {
    panelBackground(x, y, w, h);
    canvas.setTextSize(1);
    canvas.setTextColor(theme.text2);
    canvas.setCursor(x + 8, y + 6);
    canvas.print(p.title);
    canvas.setTextSize(2);
    canvas.setTextColor(theme.text);
    canvas.setCursor(x + 8, y + 17);
    canvas.print(p.value);
    drawGraph(p, x + 8, y + 36, w - 16, h - 42, 60, false);
}

// ---- Process table ----
struct TableGeom {
    int y, rows, rowH;
};

static TableGeom tableGeom() {
    if (settings.layout == LAYOUT_STACKED) return {232, 3, 24};
    return {210, 5, 19};
}

static const int TABLE_HEADER_H = 15;
static const int COL_BAR_X = 208;       // CPU bar, scaled to one full core (100%), clipped
static const int COL_BAR_W = 110;
static const int COL_CPU_RIGHT = 400;   // right edge of the CPU% value
static const int COL_MEM_RIGHT = 472;   // right edge of the MEM% value

static void drawTableHeader(int y) {
    canvas.fillRect(0, y, SCREEN_W, TABLE_HEADER_H, theme.surface);
    canvas.setTextSize(1);
    canvas.setTextColor(theme.text2);
    canvas.setCursor(8, y + 5);
    canvas.print("TOP PROCESSES");
    // Connection status (dot + label, so it does not rely on color alone).
    canvas.fillCircle(150, y + 8, 3, stale ? theme.critical : theme.good);
    canvas.setCursor(158, y + 5);
    canvas.print(stale ? (samples ? "NO HOST DATA" : "WAITING FOR HOST") : "LIVE");
    char clock[32];
    formatClock(clock, sizeof(clock));
    canvas.setTextColor(theme.text);
    printRight(clock, 340, y + 5);
    canvas.setTextColor(theme.text2);
    printRight("CPU%", COL_CPU_RIGHT, y + 5);
    printRight("MEM%", COL_MEM_RIGHT, y + 5);
}

static void drawTableRow(int i, int y, int h) {
    canvas.fillRect(0, y, SCREEN_W, h, theme.surface);
    canvas.drawFastHLine(8, y, SCREEN_W - 16, theme.grid);
    if (i >= metrics.numProcs) return;
    const Proc &p = metrics.procs[i];
    int ty = y + (h - 16) / 2 + 1;
    canvas.setTextSize(2);
    canvas.setTextColor(theme.text);
    canvas.setCursor(8, ty);
    canvas.print(p.name);
    int barW = (int)(constrain(p.cpu, 0.0f, 100.0f) / 100.0f * COL_BAR_W);
    canvas.fillRect(COL_BAR_X, y + h / 2 - 4, COL_BAR_W, 8, theme.grid);
    if (barW > 0) canvas.fillRect(COL_BAR_X, y + h / 2 - 4, barW, 8, theme.series[0]);
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f", p.cpu);
    printRight(buf, COL_CPU_RIGHT, ty);
    snprintf(buf, sizeof(buf), "%.1f", p.mem);
    printRight(buf, COL_MEM_RIGHT, ty);
}

static void drawTable() {
    TableGeom t = tableGeom();
    renderRegion(0, t.y, SCREEN_W, TABLE_HEADER_H, [&] { drawTableHeader(t.y); });
    for (int i = 0; i < t.rows; i++) {
        int ry = t.y + TABLE_HEADER_H + i * t.rowH;
        renderRegion(0, ry, SCREEN_W, t.rowH, [&] { drawTableRow(i, ry, t.rowH); });
    }
    int end = t.y + TABLE_HEADER_H + t.rows * t.rowH;
    if (end < SCREEN_H) renderRegion(0, end, SCREEN_W, SCREEN_H - end, [] { canvas.fillScreen(theme.surface); });
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
    renderRegion(0, y, SCREEN_W, TABLE_HEADER_H, [&] { drawTableHeader(y); });
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
