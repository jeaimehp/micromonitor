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
    float cpu, gpu, ram, ramUsed, ramTotal, disk, diskRead, diskWrite, netRx, netTx;
    Proc procs[NUM_PROCS];
    int numProcs;
    int32_t time;      // host unix time
    int32_t tzOffset;  // host UTC offset in seconds
};

extern Metrics metrics;
extern uint32_t samples;
extern bool stale;
extern uint32_t lastSampleMs;
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
    // size, so it is widened to cover the screen in landscape (480x320) and portrait (320x480).
    void setOrigin(int16_t x, int16_t y) {
        ox = x;
        oy = y;
        _width = SCREEN_W;
        _height = SCREEN_W;
    }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillScreen(uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void push(int16_t x, int16_t y);  // byte-swaps the buffer in place: redraw before pushing again
    // Text styles: 1 = native 5x7 pixel font; 2/3/4 = FreeSans Bold 9/12/18pt (cleaner than scaled pixel text).
    void setStyle(int style);
    // Position text by the top of its capital letters (FreeFonts are positioned by baseline).
    void cursor(int16_t x, int16_t top) { setCursor(x, top + baseline); }

private:
    int16_t ox = 0, oy = 0;
    int16_t bufW = 1, bufH = 1;
    int16_t baseline = 0;
};

enum Layout : uint8_t { LAYOUT_QUAD, LAYOUT_STACKED, LAYOUT_FOCUS, LAYOUT_TILES, LAYOUT_LCARS, LAYOUT_TRON, LAYOUT_COUNT };
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
void screenshot();                   // serial "shot": stream the frame memory to the host
uint8_t *canvasBytes();              // the canvas buffer as raw bytes (CANVAS_PIXELS * 2)
void pushRaw(int x, int y, int w, int h);  // send w*h big-endian pixels from canvasBytes() as-is
ThemeSpec BUILTIN_DEFAULT_SPEC();
bool builtinThemeNamed(const char *name);
int themeCount();                    // built-in themes, then themes from µMonitor
const char *themeName(int i);
void applyTheme(int i);  // also used at boot; out-of-range indexes fall back to theme 0
void applyRotation();
void printRight(const char *text, int right, int y);
void printCentered(const char *text, int cx, int y);
void printFit(const char *text, int maxW);  // print at the cursor, dropping trailing chars to fit maxW pixels
void formatDate(char *buf, size_t len);  // "Fri Oct 2" (no double space before single-digit days)
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
    // version 3
    uint8_t clockBadge; // show the clock badge over album pictures
    // version 4
    uint16_t slideCustom;  // custom slideshow interval in seconds (slideIdx == SLIDE_CUSTOM)
    // version 5
    uint8_t lcarsPhoto;    // show a slideshow photo in the LCARS layout
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

// ---- Content streamed from the Mac (hostcontent.cpp) ----
const int NUM_FOLDERS = 2;          // 0 = photos, 1 = motivation
const int MAX_HOST_THEMES = 12;
extern const char *const FOLDER_NAMES[NUM_FOLDERS];
void requestThemes();               // asks µMonitor for its themes ("req themes")
int hostThemeCount();
const ThemeSpec &hostTheme(int i);
void handleThemeLine(const char *line);
void handlePictureHeader(const char *line);
bool picturePending();              // a "pic" header arrived; its binary payload is next on the serial port
// Fetch picture n of a folder at w x h and draw it at (x, y). count = pictures in the folder (0 = none).
bool fetchPicture(int folder, int n, int x, int y, int w, int h, int &count);

// ---- Control (control.cpp): settings shared by the touch menu and µMonitor commands; timer/stopwatch ----
bool applySetting(const char *key, int value);  // keys: view theme layout folder slides rot badge side
int currentRotation();              // 0 normal, 1 flipped, 2 portrait, 3 portrait flipped
void reportState();                 // "state view=.. theme=.. ... themes=a,b,c" for µMonitor's menu
void handleCommand(const char *line);  // "cmd <key> <value>" / "cmd next|prev|calibrate|state"
enum TimerMode { TM_NONE, TM_TIMER, TM_STOPWATCH };
void setTimerState(int mode, float seconds, bool running);
int timerMode();
int timerSeconds();
bool timerDone();
bool timerRunning();
void formatTimer(char *buf, size_t len);
void dismissTimer();                // tap on TIME'S UP: back to the clock (also cancels it in µMonitor)
bool timerAreaHit(int x, int y);    // is (x, y) on the clock/timer area of the current view?

// ---- Album view (album.cpp) ----
// Slideshow intervals: 5s, 10s, 30s, 1 min, 5 min, 30 min, then Custom (settings.slideCustom seconds, set from µMonitor).
const int NUM_SLIDE_PRESETS = 6;
const int SLIDE_CUSTOM = NUM_SLIDE_PRESETS;  // index of the custom option
const int NUM_SLIDE_OPTIONS = NUM_SLIDE_PRESETS + 1;
extern const uint16_t SLIDE_SECONDS[NUM_SLIDE_PRESETS];
uint32_t slideSeconds();            // the current slideshow interval
void formatSlide(char *buf, size_t len);  // e.g. "every 30s", "every 5 min", "custom 2m 30s"
bool portraitActive();              // portrait rotation is in effect (any view; the menu stays landscape)
void albumShow();                   // (re)draw the current picture
void albumTick();                   // slideshow timer
bool albumTap(int x, int y);        // edges = prev/next; false for the middle (opens the menu)
void albumResetIndex();
void pictureStep(int delta);        // next/previous picture in the album or mixed view
void drawClockBadge();
bool albumBadgeHit(int x, int y);

// ---- Dashboard view (dashboard_view.cpp) ----
void ingestSample();            // add the latest metrics to the graph history
void drawDashboard();
void drawDashboardStatus();     // only the LIVE / NO HOST DATA line
bool dashboardTap(int x, int y); // true if the dashboard handled the tap (e.g. Focus layout tile select)
void drawMixedPanels();         // mixed view: everything except the photo (per sample / per minute)
void mixedShow();               // mixed view: full redraw including the photo
void mixedTick();               // mixed view slideshow
bool mixedTap(int x, int y);    // photo = next picture; false elsewhere (opens the menu)
void mixedResetIndex();
void dashboardPhoto();              // draw the dashboard's photo slot, if its layout has one (LCARS)
void mixedStep(int delta);
void drawTimerTick();               // refresh only the clock/timer area of the current view

// ---- Menu (menu.cpp) ----
extern bool menuOpen;
void openMenu();
void closeMenu();
void menuTap(int x, int y);
void menuTick();                // auto-close timeout
void redrawMenu();

// ---- Main (main.cpp) ----
void serviceSerial();
bool clockValid();
void formatClock(char *buf, size_t len);  // "" until the host has sent the time           // read and apply host samples; safe to call from blocking UI loops
void redrawView();
