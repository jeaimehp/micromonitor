#include "app.h"

// Full-screen touch menu. Buttons are large (>= 56px tall) because resistive touch is imprecise.
bool menuOpen = false;
static uint32_t lastInteraction = 0;
static const uint32_t MENU_TIMEOUT_MS = 10000;

enum ButtonId {
    B_CLOSE, B_VIEW_DASH, B_VIEW_ALBUM, B_VIEW_MIXED, B_THEME, B_LAYOUT, B_FOLDER, B_SLIDES, B_ROTATE, B_CALIBRATE,
    B_COUNT
};

struct Button {
    int16_t x, y, w, h;
};

static const int ROW_Y[4] = {52, 118, 184, 250};
static const int ROW_H = 56;
static const Button BUTTONS[B_COUNT] = {
    {372, 6, 100, 38},                                      // close
    {8, ROW_Y[0], 150, ROW_H}, {165, ROW_Y[0], 150, ROW_H}, {322, ROW_Y[0], 150, ROW_H},  // views
    {8, ROW_Y[1], 228, ROW_H}, {244, ROW_Y[1], 228, ROW_H},  // theme, layout
    {8, ROW_Y[2], 228, ROW_H}, {244, ROW_Y[2], 228, ROW_H},  // folder, slideshow
    {8, ROW_Y[3], 228, ROW_H}, {244, ROW_Y[3], 228, ROW_H},  // rotate, calibrate
};

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
        t.caption = "needs SD card";
        strcpy(t.value, "Album");
        t.enabled = false;
        break;
    case B_VIEW_MIXED:
        t.caption = "needs SD card";
        strcpy(t.value, "Mixed");
        t.enabled = false;
        break;
    case B_THEME:
        t.caption = "theme";
        strlcpy(t.value, theme.name, sizeof(t.value));
        break;
    case B_LAYOUT:
        t.caption = "layout";
        strcpy(t.value, LAYOUT_NAMES[settings.layout]);
        break;
    case B_FOLDER:
        t.caption = "album folder (needs SD)";
        strcpy(t.value, "-");
        t.enabled = false;
        break;
    case B_SLIDES:
        t.caption = "slideshow (needs SD)";
        strcpy(t.value, "-");
        t.enabled = false;
        break;
    case B_ROTATE:
        t.caption = "rotate 180";
        strcpy(t.value, settings.flip ? "Flipped" : "Normal");
        break;
    case B_CALIBRATE:
        t.caption = "touch";
        strcpy(t.value, "Calibrate");
        break;
    }
}

static void drawMenu() {
    canvas.fillScreen(theme.surface);
    canvas.setTextSize(2);
    canvas.setTextColor(theme.text);
    canvas.setCursor(16, 17);
    canvas.print("MENU");
    canvas.drawFastHLine(0, 47, SCREEN_W, theme.grid);

    for (int id = 0; id < B_COUNT; id++) {
        const Button &b = BUTTONS[id];
        ButtonText t;
        describe(id, t);
        canvas.fillRoundRect(b.x, b.y, b.w, b.h, 6, theme.button);
        uint16_t border = t.selected ? theme.accent : theme.grid;
        canvas.drawRoundRect(b.x, b.y, b.w, b.h, 6, border);
        if (t.selected) canvas.drawRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 5, border);
        int cx = b.x + b.w / 2;
        if (t.caption[0]) {
            canvas.setTextSize(1);
            canvas.setTextColor(theme.text2);
            printCentered(t.caption, cx, b.y + 9);
            canvas.setTextSize(2);
            canvas.setTextColor(t.enabled ? theme.text : theme.text2);
            printCentered(t.value, cx, b.y + 26);
        } else {
            canvas.setTextSize(2);
            canvas.setTextColor(theme.text);
            printCentered(t.value, cx, b.y + (b.h - 16) / 2);
        }
    }
}

static int hitTest(int x, int y) {
    // Accept taps a few pixels outside a button: resistive touch lands a little off.
    const int SLOP = 4;
    for (int id = 0; id < B_COUNT; id++) {
        const Button &b = BUTTONS[id];
        if (x >= b.x - SLOP && x < b.x + b.w + SLOP && y >= b.y - SLOP && y < b.y + b.h + SLOP) return id;
    }
    return -1;
}

void openMenu() {
    menuOpen = true;
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
        settings.view = VIEW_DASHBOARD;
        saveSettings();
        closeMenu();
        return;
    case B_THEME:
        // Next theme; it also brings its suggested layout (Layout can override it afterwards).
        settings.themeIdx = (settings.themeIdx + 1) % themeCount();
        applyTheme(settings.themeIdx);
        settings.layout = theme.layout;
        saveSettings();
        break;
    case B_LAYOUT:
        settings.layout = (settings.layout + 1) % LAYOUT_COUNT;
        saveSettings();
        break;
    case B_ROTATE:
        settings.flip = !settings.flip;
        saveSettings();
        applyRotation();
        break;
    case B_CALIBRATE:
        uiBusy = true;
        runCalibration();
        uiBusy = false;
        lastInteraction = millis();
        break;
    }
    renderStrips(drawMenu);
}

void menuTick() {
    if (menuOpen && millis() - lastInteraction > MENU_TIMEOUT_MS) closeMenu();
}
