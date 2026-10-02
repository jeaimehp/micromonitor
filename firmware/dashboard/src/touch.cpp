#include "app.h"

static Adafruit_STMPE610 stmpe(TS_CS);
static bool touchOk = false;
static bool injected = false;
static int injectedX, injectedY;

// Calibration targets, inset from the corners (rotation 3 frame).
static const int INSET = 30;
static const int CAL_X[4] = {INSET, SCREEN_W - 1 - INSET, SCREEN_W - 1 - INSET, INSET};
static const int CAL_Y[4] = {INSET, INSET, SCREEN_H - 1 - INSET, SCREEN_H - 1 - INSET};

bool touchBegin() {
    touchOk = stmpe.begin();
    return touchOk;
}

// Average every sample of one press (blocks until release). Returns false if there was no press.
static bool readRawPress(float &rx, float &ry) {
    if (!touchOk || !stmpe.touched()) return false;
    uint32_t n = 0, sx = 0, sy = 0, lastTouch = millis();
    while (millis() - lastTouch < 150) {
        while (!stmpe.bufferEmpty()) {
            TS_Point p = stmpe.getPoint();
            sx += p.x;
            sy += p.y;
            n++;
            lastTouch = millis();
        }
        if (stmpe.touched()) lastTouch = millis();
        stmpe.writeRegister8(STMPE_INT_STA, 0xFF);
        serviceSerial();
        delay(5);
    }
    if (n < 3) return false;
    rx = (float)sx / n;
    ry = (float)sy / n;
    return true;
}

// Raw reading -> screen coordinates in rotation 3, then mirrored when the screen is flipped 180 degrees.
static void rawToScreen(float rx, float ry, int &x, int &y) {
    const Calibration &c = settings.cal;
    float u = c.swap ? ry : rx, v = c.swap ? rx : ry;
    int sx = INSET + (int)((u - c.uL) * (SCREEN_W - 1 - 2 * INSET) / (c.uR - c.uL));
    int sy = INSET + (int)((v - c.vT) * (SCREEN_H - 1 - 2 * INSET) / (c.vB - c.vT));
    sx = constrain(sx, 0, SCREEN_W - 1);
    sy = constrain(sy, 0, SCREEN_H - 1);
    // (sx, sy) is in the rotation-3 frame; convert to the current rotation (derived from the HX8357 MADCTL bits).
    switch (tft.getRotation()) {
    case 1: x = SCREEN_W - 1 - sx; y = SCREEN_H - 1 - sy; break;
    case 0: x = sy; y = SCREEN_W - 1 - sx; break;
    case 2: x = SCREEN_H - 1 - sy; y = sx; break;
    default: x = sx; y = sy; break;
    }
}

void injectTap(int x, int y) {
    injected = true;
    injectedX = x;
    injectedY = y;
}

bool pollTap(int &x, int &y) {
    if (injected) {
        injected = false;
        x = injectedX;
        y = injectedY;
    } else {
        float rx, ry;
        if (!readRawPress(rx, ry)) return false;
        rawToScreen(rx, ry, x, y);
    }
    Serial.printlnf("tap %d %d", x, y);
    return true;
}

// ---- Guided 4-corner calibration ----
static int calStep;

static void drawCalibration() {
    canvas.fillScreen(theme.surface);
    canvas.setTextSize(2);
    canvas.setTextColor(theme.text);
    printCentered("TOUCH CALIBRATION", SCREEN_W / 2, 110);
    canvas.setTextColor(theme.text2);
    printCentered("press the center of each target", SCREEN_W / 2, 150);
    printCentered("firmly, one at a time", SCREEN_W / 2, 175);
    for (int i = 0; i < 4; i++) {
        uint16_t c = i < calStep ? theme.good : i == calStep ? theme.text : theme.grid;
        canvas.drawFastHLine(CAL_X[i] - 14, CAL_Y[i], 29, c);
        canvas.drawFastVLine(CAL_X[i], CAL_Y[i] - 14, 29, c);
        canvas.drawCircle(CAL_X[i], CAL_Y[i], 7, c);
    }
}

// Returns false (keeping the old calibration) if no target is pressed for CAL_TIMEOUT_MS.
static const uint32_t CAL_TIMEOUT_MS = 30000;

bool runCalibration() {
    // Calibrate in the unflipped frame; the flip is applied on top when mapping taps.
    uint8_t flip = settings.flip;
    settings.flip = 0;
    applyRotation();
    float rawX[4], rawY[4];
    for (calStep = 0; calStep < 4;) {
        renderStrips(drawCalibration);
        float rx, ry;
        uint32_t start = millis();
        while (!readRawPress(rx, ry)) {
            serviceSerial();
            if (millis() - start > CAL_TIMEOUT_MS) {
                settings.flip = flip;
                applyRotation();
                Serial.println("cal timeout");
                return false;
            }
        }
        rawX[calStep] = rx;
        rawY[calStep] = ry;
        Serial.printlnf("cal corner %d raw %.0f %.0f", calStep, rx, ry);
        calStep++;
    }
    renderStrips(drawCalibration);

    Calibration &c = settings.cal;
    // Whichever raw axis changes most between the two top corners runs along the screen's x axis.
    c.swap = fabsf(rawY[1] - rawY[0]) > fabsf(rawX[1] - rawX[0]);
    float u[4], v[4];
    for (int i = 0; i < 4; i++) {
        u[i] = c.swap ? rawY[i] : rawX[i];
        v[i] = c.swap ? rawX[i] : rawY[i];
    }
    c.uL = (u[0] + u[3]) / 2;
    c.uR = (u[1] + u[2]) / 2;
    c.vT = (v[0] + v[1]) / 2;
    c.vB = (v[2] + v[3]) / 2;
    settings.flip = flip;
    applyRotation();
    saveSettings();
    Serial.printlnf("cal swap=%d uL=%.0f uR=%.0f vT=%.0f vB=%.0f", c.swap, c.uL, c.uR, c.vT, c.vB);
    delay(500);
    return true;
}
