#include "app.h"

static const uint32_t SETTINGS_MAGIC = 0x584D4F4E;  // "XMON"
static const uint8_t SETTINGS_VERSION = 1;
static const int SETTINGS_ADDR = 0;

Settings settings;

static void defaults() {
    memset(&settings, 0, sizeof(settings));
    settings.magic = SETTINGS_MAGIC;
    settings.version = SETTINGS_VERSION;
    settings.view = VIEW_DASHBOARD;
    settings.slideIdx = 1;
    // Calibration measured on this wing (step 12); "Calibrate touch" in the menu replaces it.
    settings.cal = {1, 3562, 286, 516, 3537};
}

void loadSettings() {
    EEPROM.get(SETTINGS_ADDR, settings);
    if (settings.magic != SETTINGS_MAGIC || settings.version != SETTINGS_VERSION || settings.view >= VIEW_COUNT) {
        defaults();
        saveSettings();
    }
}

void saveSettings() {
    EEPROM.put(SETTINGS_ADDR, settings);
}
