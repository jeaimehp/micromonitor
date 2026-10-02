// Shared declarations for the Xenon TFT dashboard firmware.
#pragma once

#include "Particle.h"
#include "Adafruit_HX8357.h"
#include "Adafruit_STMPE610.h"
#include <functional>

// Adafruit 3.5" TFT FeatherWing on a Xenon: Feather pin 9 -> D4, 10 -> D5 (SD_CS 5 -> D2, touch CS 6 -> D3).
const int TFT_CS = D4;
const int TFT_DC = D5;
const int SD_CS = D2;
const int TS_CS = D3;
const int SCREEN_W = 480;
const int SCREEN_H = 320;

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
    int32_t time;      // host unix time
    int32_t tzOffset;  // host UTC offset in seconds
};

extern Metrics metrics;
extern uint32_t samples;
extern bool stale;
extern bool uiBusy;  // a blocking screen (e.g. calibration) owns the display; samples are not drawn

// ---- Graphics (gfx.cpp) ----
constexpr uint16_t rgb(uint32_t hex) {
    return ((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F);
}

// Off-screen canvas: everything is drawn into RAM and pushed with one DMA transfer (per-pixel SPI is far too slow).
// The origin offset lets a full-screen draw function render in horizontal strips (see renderStrips).
const size_t CANVAS_PIXELS = 240 * 110;

class Canvas : public Adafruit_GFX {
public:
    Canvas() : Adafruit_GFX(1, 1) {}
    void resize(int16_t w, int16_t h);  // buffer size; also the logical drawing size until setOrigin()
    // Draw in full-screen coordinates: buffer row 0 maps to screen (x, y). GFX clips text against the logical
    // size, so it is widened to the full screen here.
    void setOrigin(int16_t x, int16_t y) {
        ox = x;
        oy = y;
        _width = SCREEN_W;
        _height = SCREEN_H;
    }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillScreen(uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void push(int16_t x, int16_t y);  // byte-swaps the buffer in place: redraw before pushing again

private:
    int16_t ox = 0, oy = 0;
    int16_t bufW = 1, bufH = 1;
};

enum Layout : uint8_t { LAYOUT_QUAD, LAYOUT_STACKED, LAYOUT_FOCUS, LAYOUT_TILES, LAYOUT_COUNT };
extern const char *const LAYOUT_NAMES[LAYOUT_COUNT];

// Theme as stored (RGB888, in the same order as the .thm keys), and as used for drawing (RGB565).
enum ThemeColor { TC_SURFACE, TC_GRID, TC_TEXT, TC_TEXT2, TC_SERIES1, TC_SERIES2, TC_GOOD, TC_CRITICAL,
                  TC_SEPARATOR, TC_BUTTON, TC_ACCENT, TC_COUNT };

struct ThemeSpec {
    char name[16];
    uint8_t layout;
    uint32_t colors[TC_COUNT];
};

struct Theme {
    char name[16];
    uint8_t layout;  // the layout this theme suggests
    uint16_t surface, grid, text, text2, series[2], good, critical, separator, button, accent;
};

extern Adafruit_HX8357 tft;
extern Canvas canvas;
extern Theme theme;

void gfxBegin();
int themeCount();
const char *themeName(int i);
void applyTheme(int i);  // also used at boot; out-of-range indexes fall back to theme 0
void applyRotation();
void printRight(const char *text, int right, int y);
void printCentered(const char *text, int cx, int y);
// Render the screen rectangle (x, y, w, h): draw() uses absolute screen coordinates and is called once per
// horizontal strip that fits the canvas buffer.
void renderRegion(int x, int y, int w, int h, std::function<void()> draw);
// Full-width rows y0..y1 (e.g. a whole-screen menu).
void renderStrips(std::function<void()> draw, int y0 = 0, int y1 = SCREEN_H);

// ---- Settings, persisted in EEPROM (settings.cpp) ----
struct Calibration {
    uint8_t swap;          // raw y runs along screen x
    float uL, uR, vT, vB;  // raw values at the calibration inset lines (rotation 3 frame)
};

enum View : uint8_t { VIEW_DASHBOARD, VIEW_ALBUM, VIEW_MIXED, VIEW_COUNT };

struct Settings {
    uint32_t magic;
    uint8_t version;
    uint8_t view;
    uint8_t flip;       // 1 = rotate 180 degrees
    uint8_t folder;     // album folder index
    uint8_t slideIdx;   // slideshow interval index
    uint8_t themeIdx;
    uint8_t layout;
    uint8_t mixedSide;
    uint8_t albumPortrait;
    Calibration cal;
    // version 2
    uint8_t focus;      // metric shown large in the Focus layout (0 cpu, 1 ram, 2 disk, 3 net)
};

extern Settings settings;
void loadSettings();
void saveSettings();

// ---- Touch (touch.cpp) ----
bool touchBegin();
// Returns true once per completed press, with the averaged screen position in the current rotation.
bool pollTap(int &x, int &y);
void injectTap(int x, int y);   // serial "tap X Y" command, for testing without a finger
bool runCalibration();          // false if abandoned (30s without a press)

// ---- Dashboard view (dashboard_view.cpp) ----
void ingestSample();            // add the latest metrics to the graph history
void drawDashboard();
void drawDashboardStatus();     // only the LIVE / NO HOST DATA line
bool dashboardTap(int x, int y); // true if the dashboard handled the tap (e.g. Focus layout tile select)

// ---- Menu (menu.cpp) ----
extern bool menuOpen;
void openMenu();
void closeMenu();
void menuTap(int x, int y);
void menuTick();                // auto-close timeout

// ---- Main (main.cpp) ----
void serviceSerial();
bool clockValid();
void formatClock(char *buf, size_t len);  // "" until the host has sent the time           // read and apply host samples; safe to call from blocking UI loops
void redrawView();
