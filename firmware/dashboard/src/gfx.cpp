#include "app.h"

const uint32_t SPI_FREQ = 32000000;

Adafruit_HX8357 tft(TFT_CS, TFT_DC);
Canvas canvas;
static uint16_t canvasBuf[CANVAS_PIXELS];

#include "builtin_themes.h"
#include "FreeSansBold9pt7b.h"
#include "FreeSansBold12pt7b.h"
#include "FreeSansBold18pt7b.h"

void Canvas::setStyle(int style) {
    static const GFXfont *const FONTS[5] = {NULL, NULL, &FreeSansBold9pt7b, &FreeSansBold12pt7b, &FreeSansBold18pt7b};
    const GFXfont *f = FONTS[constrain(style, 1, 4)];
    setFont(f);
    setTextSize(1);
    // Offset from the top of a capital letter to the baseline: the height of 'H' above the baseline.
    baseline = f ? -(int8_t)f->glyph['H' - f->first].yOffset : 0;
}

const char *const LAYOUT_NAMES[LAYOUT_COUNT] = {"Quad", "Stacked", "Focus", "Tiles", "LCARS", "Tron"};
Theme theme;

static const int NUM_BUILTIN = sizeof(BUILTIN_THEMES) / sizeof(BUILTIN_THEMES[0]);

ThemeSpec BUILTIN_DEFAULT_SPEC() {
    return BUILTIN_THEMES[0];
}

bool builtinThemeNamed(const char *name) {
    for (int i = 0; i < NUM_BUILTIN; i++)
        if (!strcmp(BUILTIN_THEMES[i].name, name)) return true;
    return false;
}

int themeCount() {
    return NUM_BUILTIN + hostThemeCount();
}

static const ThemeSpec &themeSpec(int i) {
    return i < NUM_BUILTIN ? BUILTIN_THEMES[i] : hostTheme(i - NUM_BUILTIN);
}

const char *themeName(int i) {
    return themeSpec(i).name;
}

void applyTheme(int i) {
    if (i < 0 || i >= themeCount()) i = 0;
    const ThemeSpec &s = themeSpec(i);
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
    canvas.cp437(true);  // the classic font is code page 437, which has the micro sign at 0xE6
    tft.begin(SPI_FREQ);
    applyRotation();
    tft.fillScreen(theme.separator);
}

void applyRotation() {
    // Rotation 3 is upright as the wing is mounted; 1 is the same landscape turned 180 degrees.
    // Portrait (album only) uses 0, or 2 when flipped.
    if (portraitActive()) tft.setRotation(settings.flip ? 2 : 0);
    else tft.setRotation(settings.flip ? 1 : 3);
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

// Screenshot: read the display's frame memory (RAMRD) and send it over USB serial as
// "shot <w> <h>\n" + w*h*3 bytes (R, G, B; 6 significant bits each, left-aligned), in the current rotation.
void screenshot() {
    const int w = tft.width(), h = tft.height();
    uint8_t *row = canvasBytes();  // reuse the canvas buffer (w*3 <= 1440 bytes)
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));  // reads need a slow clock
    auto command = [](uint8_t c) {
        digitalWrite(TFT_DC, LOW);
        SPI.transfer(c);
        digitalWrite(TFT_DC, HIGH);
    };
    digitalWrite(TFT_CS, LOW);
    command(HX8357_CASET);
    SPI.transfer(0); SPI.transfer(0); SPI.transfer((w - 1) >> 8); SPI.transfer((w - 1) & 0xFF);
    command(HX8357_PASET);
    SPI.transfer(0); SPI.transfer(0); SPI.transfer((h - 1) >> 8); SPI.transfer((h - 1) & 0xFF);
    command(HX8357_RAMRD);
    SPI.transfer(0);  // dummy byte before the pixel data
    Serial.printlnf("shot %d %d", w, h);
    // One DMA transfer per row (the TX half of the canvas buffer is zero-filled), then send it.
    uint8_t *zeros = row + 2048;
    memset(zeros, 0, w * 3);
    for (int y = 0; y < h; y++) {
        SPI.transfer(zeros, row, w * 3, NULL);
        Serial.write(row, w * 3);
    }
    digitalWrite(TFT_CS, HIGH);
    SPI.endTransaction();
}

uint8_t *canvasBytes() {
    return (uint8_t *)canvasBuf;
}

void pushRaw(int x, int y, int w, int h) {
    tft.startWrite();
    tft.setAddrWindow(x, y, w, h);
    SPI.transfer(canvasBuf, NULL, (size_t)w * h * 2, NULL);
    tft.endWrite();
}

void printRight(const char *text, int right, int y) {
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    canvas.cursor(right - bw, y);
    canvas.print(text);
}

void formatDate(char *buf, size_t len) {
    strlcpy(buf, Time.format(Time.now(), "%a %b %e").c_str(), len);
    for (char *q = strstr(buf, "  "); q; q = strstr(buf, "  ")) memmove(q, q + 1, strlen(q));
}

void printFit(const char *text, int maxW) {
    char buf[40];
    strlcpy(buf, text, sizeof(buf));
    int16_t bx, by;
    uint16_t bw, bh;
    for (size_t n = strlen(buf); n > 0; buf[--n] = 0) {
        canvas.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
        if (bw <= maxW) break;
    }
    canvas.print(buf);
}

void printCentered(const char *text, int cx, int y) {
    int16_t bx, by;
    uint16_t bw, bh;
    canvas.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    canvas.cursor(cx - bw / 2, y);
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
