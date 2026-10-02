#include "app.h"

// Settings changes shared by the touch menu and µMonitor ("cmd <key> <value>"), state reports for the Mac,
// and the timer/stopwatch the Mac controls.

static int rotationOf() {
    return (settings.albumPortrait ? 2 : 0) + (settings.flip ? 1 : 0);
}

// Apply one setting (absolute value), persist it and report the new state. Returns false for unknown keys.
bool applySetting(const char *key, int v) {
    if (!strcmp(key, "view")) {
        settings.view = constrain(v, 0, VIEW_COUNT - 1);
    } else if (!strcmp(key, "theme")) {
        settings.themeIdx = constrain(v, 0, themeCount() - 1);
        applyTheme(settings.themeIdx);
        settings.layout = theme.layout;  // a theme brings its suggested layout
    } else if (!strcmp(key, "layout")) {
        settings.layout = constrain(v, 0, LAYOUT_COUNT - 1);
    } else if (!strcmp(key, "folder")) {
        settings.folder = constrain(v, 0, NUM_FOLDERS - 1);
        albumResetIndex();
        mixedResetIndex();
    } else if (!strcmp(key, "slides")) {
        settings.slideIdx = constrain(v, 0, NUM_SLIDE_OPTIONS - 1);
    } else if (!strcmp(key, "rot")) {
        v = constrain(v, 0, 3);
        settings.albumPortrait = v >= 2;
        settings.flip = v & 1;
    } else if (!strcmp(key, "badge")) {
        settings.clockBadge = v ? 1 : 0;
    } else if (!strcmp(key, "side")) {
        settings.mixedSide = v ? 1 : 0;
    } else {
        return false;
    }
    saveSettings();
    applyRotation();
    reportState();
    return true;
}

int currentRotation() {
    return rotationOf();
}

void reportState() {
    Serial.printf("state view=%d theme=%d layout=%d folder=%d slides=%d rot=%d badge=%d side=%d themes=",
                  settings.view, settings.themeIdx, settings.layout, settings.folder, settings.slideIdx, rotationOf(),
                  settings.clockBadge, settings.mixedSide);
    for (int i = 0; i < themeCount(); i++) Serial.printf("%s%s", i ? "," : "", themeName(i));
    Serial.println();
}

// "cmd <key> <value>" or "cmd next|prev|calibrate|state" from µMonitor.
void handleCommand(const char *line) {
    char key[16];
    int v = 0;
    int n = sscanf(line, "cmd %15s %d", key, &v);
    if (n < 1) return;
    if (!strcmp(key, "state")) {
        reportState();
        return;
    }
    if (!strcmp(key, "calibrate")) {
        if (menuOpen) closeMenu();
        uiBusy = true;
        runCalibration();
        uiBusy = false;
        redrawView();
        return;
    }
    if (!strcmp(key, "next") || !strcmp(key, "prev")) {
        pictureStep(!strcmp(key, "next") ? 1 : -1);
        return;
    }
    if (n == 2 && applySetting(key, v)) {
        if (menuOpen) redrawMenu();
        else redrawView();
    }
}

// ---- Timer / stopwatch (state sent by µMonitor in every sample as "tm": [mode, seconds, running]) ----
static int tmMode = TM_NONE;
static float tmBase = 0;        // seconds at the time the sample arrived (remaining for a timer, elapsed for a stopwatch)
static bool tmRunning = false;
static uint32_t tmReceived = 0;

static uint32_t dismissedAt = 0;

void setTimerState(int mode, float seconds, bool running) {
    // Samples already on their way after a tap-to-dismiss still say "done": ignore those briefly.
    if (dismissedAt && millis() - dismissedAt < 3000 && mode == TM_TIMER && seconds <= 0) return;
    tmMode = mode;
    tmBase = seconds;
    tmRunning = running;
    tmReceived = millis();
}

int timerMode() {
    return tmMode;
}

// Current value in whole seconds, ticking locally between samples.
int timerSeconds() {
    float dt = tmRunning ? (millis() - tmReceived) / 1000.0f : 0;
    float v = tmMode == TM_TIMER ? tmBase - dt : tmBase + dt;
    // A countdown shows 0:01 until it has really expired (ceil); a stopwatch counts whole elapsed seconds (floor).
    return tmMode == TM_TIMER ? max(0, (int)ceilf(v)) : max(0, (int)v);
}

bool timerDone() {
    return tmMode == TM_TIMER && timerSeconds() == 0;
}

bool timerRunning() {
    return tmRunning;
}

// Tap on a finished timer: back to the clock, and tell µMonitor to cancel it.
void dismissTimer() {
    tmMode = TM_NONE;
    dismissedAt = millis();
    Serial.println("evt timer_dismiss");
    if (settings.view == VIEW_ALBUM && !settings.clockBadge) albumShow();  // the badge area needs the picture back
    else drawTimerTick();
}

void formatTimer(char *buf, size_t len) {
    int s = timerSeconds();
    if (s >= 3600) snprintf(buf, len, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(buf, len, "%d:%02d", s / 60, s % 60);
}
