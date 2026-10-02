#include "Particle.h"
#include "Adafruit_HX8357.h"

SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);

// Adafruit 3.5" TFT FeatherWing on a Xenon: Feather pin 9 -> D4, 10 -> D5 (SD_CS 5 -> D2, touch CS 6 -> D3).
const int TFT_CS = D4;
const int TFT_DC = D5;
const int SD_CS = D2;
const int TS_CS = D3;

Adafruit_HX8357 tft(TFT_CS, TFT_DC);

// ---- Metrics received from the host (see host/collector.py for the line format) ----
const int NUM_PROCS = 5;
const int PROC_NAME_LEN = 17;

struct Proc {
    char name[PROC_NAME_LEN];
    float cpu;
    float mem;
};

struct Metrics {
    float cpu, ram, ramUsed, ramTotal, disk, diskRead, diskWrite, netRx, netTx;
    Proc procs[NUM_PROCS];
    int numProcs;
};

Metrics metrics;
uint32_t samples = 0;
uint32_t parseErrors = 0;

bool parseMetrics(const char *line, Metrics &m) {
    JSONValue root = JSONValue::parseCopy(line);
    if (!root.isObject()) {
        return false;
    }
    JSONObjectIterator it(root);
    while (it.next()) {
        JSONString key = it.name();
        JSONValue v = it.value();
        if (key == "c") m.cpu = v.toDouble();
        else if (key == "r") m.ram = v.toDouble();
        else if (key == "ru") m.ramUsed = v.toDouble();
        else if (key == "rt") m.ramTotal = v.toDouble();
        else if (key == "d") m.disk = v.toDouble();
        else if (key == "dr") m.diskRead = v.toDouble();
        else if (key == "dw") m.diskWrite = v.toDouble();
        else if (key == "nr") m.netRx = v.toDouble();
        else if (key == "nt") m.netTx = v.toDouble();
        else if (key == "p" && v.isArray()) {
            JSONArrayIterator procs(v);
            m.numProcs = 0;
            while (procs.next() && m.numProcs < NUM_PROCS) {
                JSONArrayIterator f(procs.value());
                Proc &p = m.procs[m.numProcs++];
                p.name[0] = 0;
                p.cpu = p.mem = 0;
                if (f.next()) strlcpy(p.name, (const char *)f.value().toString(), sizeof(p.name));
                if (f.next()) p.cpu = f.value().toDouble();
                if (f.next()) p.mem = f.value().toDouble();
            }
        }
    }
    return true;
}

// ---- Serial line reader ----
const size_t LINE_MAX = 1024;
char lineBuf[LINE_MAX];
size_t lineLen = 0;
bool lineOverflow = false;

// Returns true when a complete line is in lineBuf.
bool readLine() {
    while (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\r') continue;
        if (ch == '\n') {
            bool ok = !lineOverflow && lineLen > 0;
            lineBuf[lineLen] = 0;
            lineLen = 0;
            lineOverflow = false;
            if (ok) return true;
            continue;
        }
        if (lineLen < LINE_MAX - 1) lineBuf[lineLen++] = ch;
        else lineOverflow = true;
    }
    return false;
}

// ---- Off-screen rendering ----
// Per-pixel SPI writes through the GFX library are very slow on Device OS, so every region is drawn into
// a RAM canvas and pushed to the display in a single DMA transfer.
const uint32_t SPI_FREQ = 32000000;
const size_t CANVAS_PIXELS = 240 * 110;
uint16_t canvasBuf[CANVAS_PIXELS];

class Canvas : public Adafruit_GFX {
public:
    Canvas() : Adafruit_GFX(1, 1) {}

    // Reuse the shared buffer for a w x h region (w * h must be <= CANVAS_PIXELS).
    void resize(int16_t w, int16_t h) {
        _width = WIDTH = w;
        _height = HEIGHT = h;
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= _width || y >= _height) return;
        canvasBuf[y * _width + x] = color;
    }

    void fillScreen(uint16_t color) override {
        for (size_t i = 0, n = (size_t)_width * _height; i < n; i++) canvasBuf[i] = color;
    }

    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
        fillRect(x, y, w, 1, color);
    }

    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
        fillRect(x, y, 1, h, color);
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        int16_t x1 = min((int)_width, x + w), y1 = min((int)_height, y + h);
        x = max(0, (int)x);
        y = max(0, (int)y);
        for (int16_t yy = y; yy < y1; yy++) {
            uint16_t *row = canvasBuf + yy * _width;
            for (int16_t xx = x; xx < x1; xx++) row[xx] = color;
        }
    }

    // Send the canvas to the display at (x, y). The buffer is byte-swapped in place, so redraw before reuse.
    void push(int16_t x, int16_t y) {
        size_t n = (size_t)_width * _height;
        for (size_t i = 0; i < n; i++) canvasBuf[i] = __builtin_bswap16(canvasBuf[i]);
        tft.startWrite();
        tft.setAddrWindow(x, y, _width, _height);
        SPI.transfer(canvasBuf, NULL, n * 2, NULL);
        tft.endWrite();
    }
};

Canvas canvas;

// ---- Theme (dark chart surface; series colors validated for CVD separation and contrast) ----
constexpr uint16_t rgb(uint32_t hex) {
    return ((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F);
}
const uint16_t C_SURFACE = rgb(0x1a1a19);
const uint16_t C_GRID = rgb(0x383835);
const uint16_t C_TEXT = rgb(0xffffff);
const uint16_t C_TEXT2 = rgb(0xc3c2b7);
const uint16_t C_SERIES[2] = {rgb(0x3987e5), rgb(0xd95926)};

// ---- Graph panels ----
const int PANEL_W = 240;
const int PANEL_H = 105;
const int GRAPH_X = 8;
const int GRAPH_Y = 30;
const int GRAPH_W = 224;
const int GRAPH_H = 68;
const int STEP_PX = 2;
const int HISTORY = GRAPH_W / STEP_PX + 1;  // 113 samples, about 3.7 minutes at 2s

struct Panel {
    const char *title;
    int x, y;
    int numSeries;            // 0 = not implemented yet (placeholder)
    float fixedMax;           // > 0: fixed scale (e.g. 100 for %); 0: autoscale
    float hist[2][HISTORY];
    int count;                // number of valid samples (<= HISTORY)
    int head;                 // index of the next write
    char value[24];           // headline value text, top right
};

Panel panels[4] = {
    {"CPU", 0, 0, 1, 100.0f},
    {"RAM", PANEL_W, 0, 1, 100.0f},
    {"DISK", 0, PANEL_H, 0, 0.0f},
    {"NET", PANEL_W, PANEL_H, 0, 0.0f},
};
Panel &cpuPanel = panels[0];
Panel &ramPanel = panels[1];

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

void drawPanel(const Panel &p) {
    canvas.resize(PANEL_W, PANEL_H);
    canvas.fillScreen(C_SURFACE);
    // 2px separators on the right and bottom edges keep the panels visually apart.
    canvas.fillRect(PANEL_W - 2, 0, 2, PANEL_H, rgb(0x000000));
    canvas.fillRect(0, PANEL_H - 2, PANEL_W, 2, rgb(0x000000));

    canvas.setTextSize(2);
    canvas.setTextColor(C_TEXT2);
    canvas.setCursor(GRAPH_X, 7);
    canvas.print(p.title);

    if (p.numSeries == 0) {
        canvas.setTextSize(1);
        canvas.setCursor(GRAPH_X, GRAPH_Y + GRAPH_H / 2);
        canvas.print("pending");
        canvas.push(p.x, p.y);
        return;
    }

    canvas.setTextColor(C_TEXT);
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.getTextBounds(p.value, 0, 0, &bx, &by, &bw, &bh);
    canvas.setCursor(GRAPH_X + GRAPH_W - bw, 7);
    canvas.print(p.value);

    // Scale: fixed, or autoscaled to the visible max with a small floor so idle noise stays flat.
    float maxV = p.fixedMax;
    if (maxV <= 0) {
        maxV = 1.0f;
        for (int s = 0; s < p.numSeries; s++)
            for (int i = 0; i < p.count; i++) maxV = max(maxV, histAt(p, s, i));
    }

    // Recessive grid: quarter lines plus a baseline.
    for (int g = 1; g <= 3; g++) {
        int gy = GRAPH_Y + GRAPH_H * g / 4;
        for (int gx = GRAPH_X; gx < GRAPH_X + GRAPH_W; gx += 4) canvas.drawPixel(gx, gy, C_GRID);
    }
    canvas.drawFastHLine(GRAPH_X, GRAPH_Y + GRAPH_H, GRAPH_W, C_GRID);
    canvas.drawFastHLine(GRAPH_X, GRAPH_Y, GRAPH_W, C_GRID);

    // Lines (2px), newest sample at the right edge.
    int baseY = GRAPH_Y + GRAPH_H;
    for (int s = 0; s < p.numSeries; s++) {
        int prevX = -1, prevY = 0;
        for (int i = p.count - 1; i >= 0; i--) {
            float v = constrain(histAt(p, s, i), 0.0f, maxV);
            int x = GRAPH_X + GRAPH_W - 1 - i * STEP_PX;
            int y = baseY - 1 - (int)(v / maxV * (GRAPH_H - 2));
            if (prevX >= 0) {
                canvas.drawLine(prevX, prevY, x, y, C_SERIES[s]);
                canvas.drawLine(prevX, prevY - 1, x, y - 1, C_SERIES[s]);
            }
            prevX = x;
            prevY = y;
        }
    }
    canvas.push(p.x, p.y);
}

void drawTablePlaceholder() {
    canvas.resize(480, 40);
    canvas.fillScreen(C_SURFACE);
    canvas.setTextSize(1);
    canvas.setTextColor(C_TEXT2);
    canvas.setCursor(GRAPH_X, 8);
    canvas.print("TOP PROCESSES  pending");
    canvas.push(0, 2 * PANEL_H);
    canvas.fillScreen(C_SURFACE);
    for (int y = 2 * PANEL_H + 40; y < 320; y += 40) {
        canvas.fillScreen(C_SURFACE);
        canvas.push(0, y);
    }
}

void updateDashboard() {
    pushSample(cpuPanel, metrics.cpu);
    snprintf(cpuPanel.value, sizeof(cpuPanel.value), "%.1f%%", metrics.cpu);
    pushSample(ramPanel, metrics.ram);
    snprintf(ramPanel.value, sizeof(ramPanel.value), "%.0f/%.0fG %.0f%%", metrics.ramUsed, metrics.ramTotal,
             metrics.ram);
    for (const Panel &p : panels) drawPanel(p);
}

void setup() {
    Serial.begin(115200);
    // Keep other SPI devices on the wing deselected.
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(TS_CS, OUTPUT);
    digitalWrite(TS_CS, HIGH);

    tft.begin(SPI_FREQ);
    tft.setRotation(3);  // landscape 480x320, flipped to match how the wing is mounted
    tft.fillScreen(HX8357_BLACK);
    for (const Panel &p : panels) drawPanel(p);
    drawTablePlaceholder();
}

void loop() {
    if (!readLine()) return;
    if (!parseMetrics(lineBuf, metrics)) {
        parseErrors++;
        Serial.printlnf("err %lu", (unsigned long)parseErrors);
        return;
    }
    samples++;
    uint32_t t0 = millis();
    updateDashboard();
    Serial.printlnf("ack %lu c=%.1f p=%d draw=%lums", (unsigned long)samples, metrics.cpu, metrics.numProcs,
                    (unsigned long)(millis() - t0));
}
