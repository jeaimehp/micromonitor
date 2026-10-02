#include "app.h"

static const uint32_t SETTINGS_MAGIC = 0x584D4F4E;  // "XMON"
static const uint8_t SETTINGS_VERSION = 3;
static const int SETTINGS_ADDR = 0;

Settings settings;

static void defaults() {
    memset(&settings, 0, sizeof(settings));
    settings.magic = SETTINGS_MAGIC;
    settings.version = SETTINGS_VERSION;
    settings.view = VIEW_DASHBOARD;
    settings.slideIdx = 1;
    settings.clockBadge = 1;
    // Calibration measured on this wing (step 12); "Calibrate touch" in the menu replaces it.
    settings.cal = {1, 3562, 286, 516, 3537};
}

void loadSettings() {
    EEPROM.get(SETTINGS_ADDR, settings);
    if (settings.magic != SETTINGS_MAGIC || settings.version == 0 || settings.version > SETTINGS_VERSION ||
        settings.view >= VIEW_COUNT) {
        defaults();
        saveSettings();
        return;
    }
    // Upgrade older layouts in place, keeping calibration and earlier choices.
    if (settings.version < 2) {
        settings.focus = 0;
        settings.themeIdx = 0;
        settings.layout = LAYOUT_QUAD;
    }
    if (settings.version < 3) settings.clockBadge = 1;
    if (settings.layout >= LAYOUT_COUNT) settings.layout = LAYOUT_QUAD;
    if (settings.folder >= NUM_FOLDERS) settings.folder = 0;
    if (settings.slideIdx >= NUM_SLIDE_OPTIONS) settings.slideIdx = 1;
    if (settings.focus > 3) settings.focus = 0;
    if (settings.version != SETTINGS_VERSION) {
        settings.version = SETTINGS_VERSION;
        saveSettings();
    }
}

void saveSettings() {
    EEPROM.put(SETTINGS_ADDR, settings);
}
