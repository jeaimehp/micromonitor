#include "app.h"

// Full-screen touch menu. Buttons are large (>= 56px tall) because resistive touch is imprecise.
bool menuOpen = false;
static uint32_t lastInteraction = 0;
static const uint32_t MENU_TIMEOUT_MS = 10000;

enum ButtonId {
    B_CLOSE, B_VIEW_DASH, B_VIEW_ALBUM, B_VIEW_MIXED, B_THEME, B_LAYOUT, B_FOLDER, B_SLIDES, B_ROTATE, B_MORE,
    B_BADGE, B_SIDE, B_CALIBRATE, B_LPHOTO, B_BACK, B_COUNT
};

struct Button {
    int16_t x, y, w, h;
    uint8_t page;  // 0 = main page, 1 = "More" page; 255 = both
};

static int page = 0;
static const int ROW_Y[4] = {52, 118, 184, 250};
static const int ROW_H = 56;
static const Button BUTTONS[B_COUNT] = {
    {372, 6, 100, 38, 255},                                                             // close
    {8, ROW_Y[0], 150, ROW_H, 0}, {165, ROW_Y[0], 150, ROW_H, 0}, {322, ROW_Y[0], 150, ROW_H, 0},  // views
    {8, ROW_Y[1], 228, ROW_H, 0}, {244, ROW_Y[1], 228, ROW_H, 0},                       // theme, layout
    {8, ROW_Y[2], 228, ROW_H, 0}, {244, ROW_Y[2], 228, ROW_H, 0},                       // folder, slideshow
    {8, ROW_Y[3], 228, ROW_H, 0}, {244, ROW_Y[3], 228, ROW_H, 0},                       // rotate, more
    {8, ROW_Y[0], 228, ROW_H, 1}, {244, ROW_Y[0], 228, ROW_H, 1},                       // clock badge, photo side
    {8, ROW_Y[1], 228, ROW_H, 1}, {244, ROW_Y[1], 228, ROW_H, 1},                       // calibrate, LCARS photo
    {244, ROW_Y[3], 228, ROW_H, 1},                                                     // back
};

static const char *const ROTATION_NAMES[4] = {"Normal", "Flipped", "Portrait", "Portrait flip"};

static int rotationIndex() {
    return currentRotation();
}

// Caption (small, top line) and value (large) for each button; enabled = false greys it out with a reason.
struct ButtonText {
    const char *caption;
    char value[24];
    bool enabled;
    bool selected;
};

static void describe(int id, ButtonText &t) {
    t.caption = "";
    t.value[0] = 0;
    t.enabled = true;
    t.selected = false;
    switch (id) {
    case B_CLOSE: strcpy(t.value, "CLOSE"); break;
    case B_VIEW_DASH:
        t.caption = "view";
        strcpy(t.value, "Dashboard");
        t.selected = settings.view == VIEW_DASHBOARD;
        break;
    case B_VIEW_ALBUM:
        t.caption = "view";
        strcpy(t.value, "Album");
        t.selected = settings.view == VIEW_ALBUM;
        break;
    case B_VIEW_MIXED:
        t.caption = "view";
        strcpy(t.value, "Mixed");
        t.selected = settings.view == VIEW_MIXED;
        break;
    case B_THEME:
        t.caption = "theme";
        strlcpy(t.value, theme.name, sizeof(t.value));
        break;
    case B_LAYOUT:
        t.caption = "dashboard layout";
        strcpy(t.value, LAYOUT_NAMES[settings.layout]);
        break;
    case B_FOLDER:
        t.caption = "album pictures";
        strcpy(t.value, FOLDER_NAMES[settings.folder]);
        break;
    case B_SLIDES:
        t.caption = "slideshow";
        formatSlide(t.value, sizeof(t.value));
        break;
    case B_ROTATE:
        t.caption = "rotate";
        strcpy(t.value, ROTATION_NAMES[rotationIndex()]);
        break;
    case B_MORE: strcpy(t.value, "More..."); break;
    case B_BADGE:
        t.caption = "clock on pictures";
        strcpy(t.value, settings.clockBadge ? "On" : "Off");
        break;
    case B_SIDE:
        t.caption = "mixed view photo";
        strcpy(t.value, settings.mixedSide ? "Right" : "Left");
        break;
    case B_CALIBRATE:
        t.caption = "touch";
        strcpy(t.value, "Calibrate");
        break;
    case B_LPHOTO:
        t.caption = "photo in LCARS/XP/Mac layout";
        strcpy(t.value, settings.lcarsPhoto ? "On" : "Off");
        break;
    case B_BACK: strcpy(t.value, "Back"); break;
    }
}

static bool onPage(int id) {
    return BUTTONS[id].page == 255 || BUTTONS[id].page == page;
}

static void drawMenu() {
    canvas.fillScreen(theme.surface);
    canvas.setStyle(2);
    canvas.setTextColor(theme.text);
    canvas.cursor(16, 17);
    canvas.print(page ? "MENU > MORE" : "MENU");
    canvas.drawFastHLine(0, 47, SCREEN_W, theme.grid);

    for (int id = 0; id < B_COUNT; id++) {
        if (!onPage(id)) continue;
        const Button &b = BUTTONS[id];
        ButtonText t;
        describe(id, t);
        canvas.fillRoundRect(b.x, b.y, b.w, b.h, 6, theme.button);
        uint16_t border = t.selected ? theme.accent : theme.grid;
        canvas.drawRoundRect(b.x, b.y, b.w, b.h, 6, border);
        if (t.selected) canvas.drawRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 5, border);
        int cx = b.x + b.w / 2;
        if (t.caption[0]) {
            canvas.setStyle(1);
            canvas.setTextColor(theme.text2);
            printCentered(t.caption, cx, b.y + 9);
            canvas.setStyle(2);
            canvas.setTextColor(t.enabled ? theme.text : theme.text2);
            printCentered(t.value, cx, b.y + 26);
        } else {
            canvas.setStyle(2);
            canvas.setTextColor(theme.text);
            printCentered(t.value, cx, b.y + (b.h - 16) / 2);
        }
    }
}

static int hitTest(int x, int y) {
    // Accept taps a few pixels outside a button: resistive touch lands a little off.
    const int SLOP = 4;
    for (int id = 0; id < B_COUNT; id++) {
        if (!onPage(id)) continue;
        const Button &b = BUTTONS[id];
        if (x >= b.x - SLOP && x < b.x + b.w + SLOP && y >= b.y - SLOP && y < b.y + b.h + SLOP) return id;
    }
    return -1;
}

void openMenu() {
    menuOpen = true;
    page = 0;
    applyRotation();  // the menu is always landscape, even over a portrait album
    lastInteraction = millis();
    renderStrips(drawMenu);
    Serial.println("menu open");
}

void closeMenu() {
    menuOpen = false;
    Serial.println("menu closed");
    redrawView();
}

void menuTap(int x, int y) {
    lastInteraction = millis();
    int id = hitTest(x, y);
    ButtonText t;
    if (id >= 0) describe(id, t);
    if (id < 0 || id == B_CLOSE) {
        closeMenu();
        return;
    }
    if (!t.enabled) return;
    Serial.printlnf("menu button %d", id);
    switch (id) {
    case B_VIEW_DASH:
    case B_VIEW_ALBUM:
    case B_VIEW_MIXED:
        applySetting("view", id == B_VIEW_DASH ? VIEW_DASHBOARD : id == B_VIEW_ALBUM ? VIEW_ALBUM : VIEW_MIXED);
        closeMenu();
        return;
    case B_THEME: applySetting("theme", (settings.themeIdx + 1) % themeCount()); break;
    case B_LAYOUT: applySetting("layout", (settings.layout + 1) % LAYOUT_COUNT); break;
    case B_FOLDER: applySetting("folder", (settings.folder + 1) % NUM_FOLDERS); break;
    case B_SLIDES:
        // Cycle the presets, then the custom interval (set from µMonitor), then back to the first preset.
        applySetting("slides", (settings.slideIdx + 1) % NUM_SLIDE_OPTIONS);
        break;
    case B_ROTATE: applySetting("rot", (rotationIndex() + 1) % 4); break;
    case B_MORE:
    case B_BACK:
        page = id == B_MORE ? 1 : 0;
        break;
    case B_BADGE: applySetting("badge", !settings.clockBadge); break;
    case B_SIDE: applySetting("side", !settings.mixedSide); break;
    case B_LPHOTO: applySetting("lphoto", !settings.lcarsPhoto); break;
    case B_CALIBRATE:
        uiBusy = true;
        runCalibration();
        uiBusy = false;
        lastInteraction = millis();
        break;
    }
    renderStrips(drawMenu);
}

void redrawMenu() {
    lastInteraction = millis();
    renderStrips(drawMenu);
}

void menuTick() {
    if (menuOpen && millis() - lastInteraction > MENU_TIMEOUT_MS) closeMenu();
}
