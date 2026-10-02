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

// ---- Drawing ----
void drawStatus() {
    canvas.resize(480, 40);
    for (int row = 0; row < 7; row++) {
        canvas.fillScreen(HX8357_BLACK);
        canvas.setTextSize(2);
        canvas.setTextColor(HX8357_WHITE);
        canvas.setCursor(10, 12);
        if (row == 0) {
            canvas.printf("samples %lu errors %lu free %lu", (unsigned long)samples, (unsigned long)parseErrors,
                          (unsigned long)System.freeMemory());
        } else if (row == 1) {
            canvas.printf("cpu %5.1f%%  ram %5.1f%%  disk %5.1f%%", metrics.cpu, metrics.ram, metrics.disk);
        } else {
            int i = row - 2;
            if (i < metrics.numProcs) {
                const Proc &p = metrics.procs[i];
                canvas.printf("%-16s %6.1f %5.1f", p.name, p.cpu, p.mem);
            }
        }
        canvas.push(0, row * 40);
    }
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
    tft.setTextSize(2);
    tft.setTextColor(HX8357_WHITE);
    tft.setCursor(10, 10);
    tft.print("waiting for host...");
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
    drawStatus();
    Serial.printlnf("ack %lu c=%.1f p=%d draw=%lums", (unsigned long)samples, metrics.cpu, metrics.numProcs,
                    (unsigned long)(millis() - t0));
}
