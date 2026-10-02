#include "app.h"

const uint32_t SPI_FREQ = 32000000;

Adafruit_HX8357 tft(TFT_CS, TFT_DC);
Canvas canvas;
static uint16_t canvasBuf[CANVAS_PIXELS];

// Built-in theme (dark chart surface; series colors validated for CVD separation and contrast).
Theme theme = {
    "Dark",
    rgb(0x1a1a19),                      // surface
    rgb(0x383835),                      // grid
    rgb(0xffffff),                      // text
    rgb(0xc3c2b7),                      // text2
    {rgb(0x3987e5), rgb(0xd95926)},     // series
    rgb(0x0ca30c),                      // good
    rgb(0xd03b3b),                      // critical
    rgb(0x000000),                      // separator
    rgb(0x2c2c2a),                      // button
    rgb(0x3987e5),                      // accent
};

void gfxBegin() {
    tft.begin(SPI_FREQ);
    applyRotation();
    tft.fillScreen(theme.separator);
}

void applyRotation() {
    // Rotation 3 is upright as the wing is mounted; 1 is the same landscape turned 180 degrees.
    tft.setRotation(settings.flip ? 1 : 3);
}

void Canvas::resize(int16_t w, int16_t h) {
    _width = WIDTH = bufW = w;
    _height = HEIGHT = bufH = h;
    ox = oy = 0;
}

void Canvas::drawPixel(int16_t x, int16_t y, uint16_t color) {
    x -= ox;
    y -= oy;
    if (x < 0 || y < 0 || x >= bufW || y >= bufH) return;
    canvasBuf[y * bufW + x] = color;
}

void Canvas::fillScreen(uint16_t color) {
    for (size_t i = 0, n = (size_t)bufW * bufH; i < n; i++) canvasBuf[i] = color;
}

void Canvas::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    fillRect(x, y, w, 1, color);
}

void Canvas::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    fillRect(x, y, 1, h, color);
}

void Canvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    x -= ox;
    y -= oy;
    int16_t x1 = min((int)bufW, x + w), y1 = min((int)bufH, y + h);
    x = max(0, (int)x);
    y = max(0, (int)y);
    for (int16_t yy = y; yy < y1; yy++) {
        uint16_t *row = canvasBuf + yy * bufW;
        for (int16_t xx = x; xx < x1; xx++) row[xx] = color;
    }
}

void Canvas::push(int16_t x, int16_t y) {
    size_t n = (size_t)bufW * bufH;
    for (size_t i = 0; i < n; i++) canvasBuf[i] = __builtin_bswap16(canvasBuf[i]);
    tft.startWrite();
    tft.setAddrWindow(x, y, bufW, bufH);
    SPI.transfer(canvasBuf, NULL, n * 2, NULL);
    tft.endWrite();
}

void printRight(const char *text, int right, int y) {
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    canvas.setCursor(right - bw, y);
    canvas.print(text);
}

void printCentered(const char *text, int cx, int y) {
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    canvas.setCursor(cx - bw / 2, y);
    canvas.print(text);
}

void renderStrips(void (*draw)(), int y0, int y1, int stripH) {
    for (int y = y0; y < y1; y += stripH) {
        int h = min(stripH, y1 - y);
        canvas.resize(SCREEN_W, h);
        canvas.setOrigin(0, y);
        draw();
        canvas.push(0, y);
    }
}
