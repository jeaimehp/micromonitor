#include "app.h"

const uint32_t SPI_FREQ = 32000000;

Adafruit_HX8357 tft(TFT_CS, TFT_DC);
Canvas canvas;
static uint16_t canvasBuf[CANVAS_PIXELS];

#include "builtin_themes.h"

const char *const LAYOUT_NAMES[LAYOUT_COUNT] = {"Quad", "Stacked", "Focus", "Tiles"};
Theme theme;

int themeCount() {
    return sizeof(BUILTIN_THEMES) / sizeof(BUILTIN_THEMES[0]);
}

const char *themeName(int i) {
    return BUILTIN_THEMES[i].name;
}

void applyTheme(int i) {
    if (i < 0 || i >= themeCount()) i = 0;
    const ThemeSpec &s = BUILTIN_THEMES[i];
    strlcpy(theme.name, s.name, sizeof(theme.name));
    theme.layout = s.layout;
    const uint32_t *c = s.colors;
    theme.surface = rgb(c[TC_SURFACE]);
    theme.grid = rgb(c[TC_GRID]);
    theme.text = rgb(c[TC_TEXT]);
    theme.text2 = rgb(c[TC_TEXT2]);
    theme.series[0] = rgb(c[TC_SERIES1]);
    theme.series[1] = rgb(c[TC_SERIES2]);
    theme.good = rgb(c[TC_GOOD]);
    theme.critical = rgb(c[TC_CRITICAL]);
    theme.separator = rgb(c[TC_SEPARATOR]);
    theme.button = rgb(c[TC_BUTTON]);
    theme.accent = rgb(c[TC_ACCENT]);
}

void gfxBegin() {
    applyTheme(settings.themeIdx);
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

void renderRegion(int x, int y, int w, int h, std::function<void()> draw) {
    int stripH = min(h, (int)(CANVAS_PIXELS / w));
    for (int sy = y; sy < y + h; sy += stripH) {
        int sh = min(stripH, y + h - sy);
        canvas.resize(w, sh);
        canvas.setOrigin(x, sy);
        draw();
        canvas.push(x, sy);
    }
}

void renderStrips(std::function<void()> draw, int y0, int y1) {
    renderRegion(0, y0, SCREEN_W, y1 - y0, draw);
}
