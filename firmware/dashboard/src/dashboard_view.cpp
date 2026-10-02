#include "app.h"

// ---- Graph panels ----
const int PANEL_W = 240;
const int PANEL_H = 105;
const int GRAPH_X = 8;
const int GRAPH_Y = 36;
const int GRAPH_W = 224;
const int GRAPH_H = 62;
const int STEP_PX = 2;
const int HISTORY = GRAPH_W / STEP_PX + 1;  // 113 samples, about 3.7 minutes at 2s

struct Panel {
    const char *title;
    int x, y;
    int numSeries;            // 0 = not implemented yet (placeholder)
    float fixedMax;           // > 0: fixed scale (e.g. 100 for %); 0: autoscale
    const char *labels[2];    // legend labels for 2-series panels
    const char *unit;         // unit of the series values, shown with the autoscale max
    float hist[2][HISTORY];
    int count;                // number of valid samples (<= HISTORY)
    int head;                 // index of the next write
    char value[24];           // headline value text, top right
};

Panel panels[4] = {
    {"CPU", 0, 0, 1, 100.0f},
    {"RAM", PANEL_W, 0, 1, 100.0f},
    {"DISK", 0, PANEL_H, 2, 0.0f, {"rd", "wr"}, "MB/s"},
    {"NET", PANEL_W, PANEL_H, 2, 0.0f, {"rx", "tx"}, "KB/s"},
};
Panel &cpuPanel = panels[0];
Panel &ramPanel = panels[1];
Panel &diskPanel = panels[2];
Panel &netPanel = panels[3];

void pushSample(Panel &p, float a, float b = 0) {
    p.hist[0][p.head] = a;
    p.hist[1][p.head] = b;
    p.head = (p.head + 1) % HISTORY;
    if (p.count < HISTORY) p.count++;
}

// Value of series s, i samples back from the newest (i = 0 is newest).
float histAt(const Panel &p, int s, int i) {
    return p.hist[s][(p.head - 1 - i + HISTORY) % HISTORY];
}

// Round up to 1, 2 or 5 x 10^n (minimum 1) so the autoscale max reads cleanly.
float niceCeil(float v) {
    float step = 1.0f;
    while (step < v) {
        if (step * 2 >= v) return step * 2;
        if (step * 5 >= v) return step * 5;
        step *= 10;
    }
    return step;
}

void drawPanel(const Panel &p) {
    canvas.resize(PANEL_W, PANEL_H);
    canvas.fillScreen(theme.surface);
    // 2px separators on the right and bottom edges keep the panels visually apart.
    canvas.fillRect(PANEL_W - 2, 0, 2, PANEL_H, theme.separator);
    canvas.fillRect(0, PANEL_H - 2, PANEL_W, 2, theme.separator);

    canvas.setTextSize(2);
    canvas.setTextColor(theme.text2);
    canvas.setCursor(GRAPH_X, 7);
    canvas.print(p.title);

    if (p.numSeries == 0) {
        canvas.setTextSize(1);
        canvas.setCursor(GRAPH_X, GRAPH_Y + GRAPH_H / 2);
        canvas.print("pending");
        canvas.push(p.x, p.y);
        return;
    }

    canvas.setTextColor(theme.text);
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.getTextBounds(p.value, 0, 0, &bx, &by, &bw, &bh);
    canvas.setCursor(GRAPH_X + GRAPH_W - bw, 7);
    canvas.print(p.value);

    // Scale: fixed, or autoscaled to the visible max with a small floor so idle noise stays flat.
    float maxV = p.fixedMax;
    if (maxV <= 0) {
        float peak = 0;
        for (int s = 0; s < p.numSeries; s++)
            for (int i = 0; i < p.count; i++) peak = max(peak, histAt(p, s, i));
        maxV = niceCeil(peak);
    }

    // Legend row: a colored swatch per series, label + latest value in text ink; autoscale max on the right.
    canvas.setTextSize(1);
    if (p.numSeries == 2) {
        int lx = GRAPH_X;
        for (int s = 0; s < 2; s++) {
            canvas.fillRect(lx, 25, 8, 8, theme.series[s]);
            canvas.setTextColor(theme.text2);
            canvas.setCursor(lx + 12, 25);
            char buf[20];
            float v = p.count ? histAt(p, s, 0) : 0.0f;
            snprintf(buf, sizeof(buf), v < 10 ? "%s %.1f" : "%s %.0f", p.labels[s], v);
            canvas.print(buf);
            lx += 12 + strlen(buf) * 6 + 10;
        }
    }
    if (p.fixedMax <= 0) {
        char buf[20];
        snprintf(buf, sizeof(buf), "max %g %s", maxV, p.unit);
        canvas.setTextColor(theme.text2);
        canvas.setCursor(GRAPH_X + GRAPH_W - strlen(buf) * 6, 25);
        canvas.print(buf);
    }

    // Recessive grid: quarter lines plus a baseline.
    for (int g = 1; g <= 3; g++) {
        int gy = GRAPH_Y + GRAPH_H * g / 4;
        for (int gx = GRAPH_X; gx < GRAPH_X + GRAPH_W; gx += 4) canvas.drawPixel(gx, gy, theme.grid);
    }
    canvas.drawFastHLine(GRAPH_X, GRAPH_Y + GRAPH_H, GRAPH_W, theme.grid);
    canvas.drawFastHLine(GRAPH_X, GRAPH_Y, GRAPH_W, theme.grid);

    // Lines (2px), newest sample at the right edge.
    int baseY = GRAPH_Y + GRAPH_H;
    for (int s = 0; s < p.numSeries; s++) {
        int prevX = -1, prevY = 0;
        for (int i = p.count - 1; i >= 0; i--) {
            float v = constrain(histAt(p, s, i), 0.0f, maxV);
            int x = GRAPH_X + GRAPH_W - 1 - i * STEP_PX;
            int y = baseY - 1 - (int)(v / maxV * (GRAPH_H - 2));
            if (prevX >= 0) {
                canvas.drawLine(prevX, prevY, x, y, theme.series[s]);
                canvas.drawLine(prevX, prevY - 1, x, y - 1, theme.series[s]);
            }
            prevX = x;
            prevY = y;
        }
    }
    canvas.push(p.x, p.y);
}

// ---- Top-5 process table (y 210..320): a header strip + 5 row strips, each pushed separately ----
const int TABLE_Y = 2 * PANEL_H;
const int TABLE_HEADER_H = 15;
const int TABLE_ROW_H = 19;
const int COL_BAR_X = 208;       // CPU bar, scaled to one full core (100%), clipped
const int COL_BAR_W = 110;
const int COL_CPU_RIGHT = 400;   // right edge of the CPU% value
const int COL_MEM_RIGHT = 472;   // right edge of the MEM% value

void drawTable() {
    canvas.resize(480, TABLE_HEADER_H);
    canvas.fillScreen(theme.surface);
    canvas.setTextSize(1);
    canvas.setTextColor(theme.text2);
    canvas.setCursor(GRAPH_X, 5);
    canvas.print("TOP PROCESSES");
    // Connection status (dot + label, so it does not rely on color alone).
    canvas.fillCircle(150, 8, 3, stale ? theme.critical : theme.good);
    canvas.setCursor(158, 5);
    canvas.print(stale ? (samples ? "NO HOST DATA" : "WAITING FOR HOST") : "LIVE");
    printRight("CPU%", COL_CPU_RIGHT, 5);
    printRight("MEM%", COL_MEM_RIGHT, 5);
    canvas.push(0, TABLE_Y);

    canvas.resize(480, TABLE_ROW_H);
    for (int i = 0; i < NUM_PROCS; i++) {
        canvas.fillScreen(theme.surface);
        canvas.drawFastHLine(GRAPH_X, 0, 480 - 2 * GRAPH_X, theme.grid);
        if (i < metrics.numProcs) {
            const Proc &p = metrics.procs[i];
            canvas.setTextSize(2);
            canvas.setTextColor(theme.text);
            canvas.setCursor(GRAPH_X, 2);
            canvas.print(p.name);

            int barW = (int)(constrain(p.cpu, 0.0f, 100.0f) / 100.0f * COL_BAR_W);
            canvas.fillRect(COL_BAR_X, 5, COL_BAR_W, 8, theme.grid);
            if (barW > 0) canvas.fillRect(COL_BAR_X, 5, barW, 8, theme.series[0]);

            char buf[12];
            snprintf(buf, sizeof(buf), "%.1f", p.cpu);
            printRight(buf, COL_CPU_RIGHT, 2);
            snprintf(buf, sizeof(buf), "%.1f", p.mem);
            printRight(buf, COL_MEM_RIGHT, 2);
        }
        canvas.push(0, TABLE_Y + TABLE_HEADER_H + i * TABLE_ROW_H);
    }
}

void ingestSample() {
    pushSample(cpuPanel, metrics.cpu);
    snprintf(cpuPanel.value, sizeof(cpuPanel.value), "%.1f%%", metrics.cpu);
    pushSample(ramPanel, metrics.ram);
    snprintf(ramPanel.value, sizeof(ramPanel.value), "%.0f/%.0fG %.0f%%", metrics.ramUsed, metrics.ramTotal,
             metrics.ram);
    pushSample(diskPanel, metrics.diskRead, metrics.diskWrite);
    snprintf(diskPanel.value, sizeof(diskPanel.value), "%.0f%% used", metrics.disk);
    pushSample(netPanel, metrics.netRx, metrics.netTx);
    float total = metrics.netRx + metrics.netTx;
    if (total < 1000) snprintf(netPanel.value, sizeof(netPanel.value), "%.0f KB/s", total);
    else snprintf(netPanel.value, sizeof(netPanel.value), "%.1f MB/s", total / 1000);
}

void drawDashboard() {
    for (const Panel &p : panels) drawPanel(p);
    drawTable();
}

void drawDashboardStatus() {
    drawTable();
}
