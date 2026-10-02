#include "Particle.h"
#include "Adafruit_HX8357.h"
#include "Adafruit_STMPE610.h"

// Touch bring-up: detect which controller the FeatherWing has, then print and plot raw touch readings.
SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);

const int TFT_CS = D4, TFT_DC = D5, SD_CS = D2, TS_CS = D3;
const uint8_t TSC2007_ADDR = 0x48;

Adafruit_HX8357 tft(TFT_CS, TFT_DC);
Adafruit_STMPE610 stmpe(TS_CS);

enum Controller { NONE, STMPE610, TSC2007 } controller = NONE;
uint16_t stmpeVersion = 0;


// TSC2007: command = function << 4 | power << 2 | 12-bit mode; result is 12 bits in the top of 2 bytes.
uint16_t tscCommand(uint8_t func, uint8_t power) {
    Wire.beginTransmission(TSC2007_ADDR);
    Wire.write((func << 4) | (power << 2));
    Wire.endTransmission();
    delayMicroseconds(200);
    Wire.requestFrom(TSC2007_ADDR, (uint8_t)2);
    uint16_t hi = Wire.read(), lo = Wire.read();
    return (hi << 4) | (lo >> 4);
}

bool tscRead(uint16_t &x, uint16_t &y, uint16_t &z1) {
    x = tscCommand(0xC, 1);   // MEASURE_X, ADC on / IRQ off
    y = tscCommand(0xD, 1);   // MEASURE_Y
    z1 = tscCommand(0xE, 1);  // MEASURE_Z1 (pressure)
    tscCommand(0x0, 0);       // power down, IRQ on
    return z1 > 100;
}

void say(int line, const char *text) {
    tft.fillRect(0, 100 + line * 24, 480, 22, HX8357_BLACK);
    tft.setCursor(60, 102 + line * 24);
    tft.print(text);
    Serial.println(text);
}

// ---- Guided 4-corner calibration ----
const int INSET = 30;
const int TGT_X[4] = {INSET, 479 - INSET, 479 - INSET, INSET};
const int TGT_Y[4] = {INSET, INSET, 319 - INSET, 319 - INSET};
const char *CORNER[4] = {"top-left", "top-right", "bottom-right", "bottom-left"};
float rawX[4], rawY[4];
int corner = 0;
bool calibrated = false;
bool swapAxes = false;
float uL, uR, vT, vB;  // calibrated raw values at the inset lines

void drawTarget(int i, uint16_t color) {
    tft.drawFastHLine(TGT_X[i] - 12, TGT_Y[i], 25, color);
    tft.drawFastVLine(TGT_X[i], TGT_Y[i] - 12, 25, color);
    tft.drawCircle(TGT_X[i], TGT_Y[i], 6, color);
}

// Average every sample of one press; returns false if there was no press.
bool readPress(float &x, float &y) {
    if (!stmpe.touched()) return false;
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
        delay(5);
    }
    if (n < 3) return false;
    x = (float)sx / n;
    y = (float)sy / n;
    return true;
}

void finishCalibration() {
    // Whichever raw axis changes most between the two top corners runs along the screen's x axis.
    swapAxes = fabsf(rawY[1] - rawY[0]) > fabsf(rawX[1] - rawX[0]);
    float u[4], v[4];
    for (int i = 0; i < 4; i++) {
        u[i] = swapAxes ? rawY[i] : rawX[i];
        v[i] = swapAxes ? rawX[i] : rawY[i];
    }
    uL = (u[0] + u[3]) / 2;
    uR = (u[1] + u[2]) / 2;
    vT = (v[0] + v[1]) / 2;
    vB = (v[2] + v[3]) / 2;
    calibrated = true;
    char buf[96];
    snprintf(buf, sizeof(buf), "cal swap=%d uL=%.0f uR=%.0f vT=%.0f vB=%.0f", swapAxes, uL, uR, vT, vB);
    tft.fillScreen(HX8357_BLACK);
    say(0, "calibrated - tap anywhere");
    say(1, buf + 4);
    Serial.println(buf);
}

void toScreen(float rx, float ry, int &sx, int &sy) {
    float u = swapAxes ? ry : rx, v = swapAxes ? rx : ry;
    sx = INSET + (int)((u - uL) * (479 - 2 * INSET) / (uR - uL));
    sy = INSET + (int)((v - vT) * (319 - 2 * INSET) / (vB - vT));
}

void setup() {
    Serial.begin(115200);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(TS_CS, OUTPUT);
    digitalWrite(TS_CS, HIGH);
    tft.begin(32000000);
    tft.setRotation(3);
    tft.fillScreen(HX8357_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(HX8357_WHITE);

    if (stmpe.begin()) {
        controller = STMPE610;
        stmpeVersion = stmpe.getVersion();
    }
    say(0, controller == STMPE610 ? "STMPE610 found" : "NO touch controller!");
    say(1, "press the yellow target");
    drawTarget(0, HX8357_YELLOW);
}

void loop() {
    float rx, ry;
    if (controller != STMPE610 || !readPress(rx, ry)) return;
    if (!calibrated) {
        rawX[corner] = rx;
        rawY[corner] = ry;
        Serial.printlnf("corner %d %s raw x=%.0f y=%.0f", corner, CORNER[corner], rx, ry);
        drawTarget(corner, HX8357_GREEN);
        if (++corner == 4) {
            delay(400);
            finishCalibration();
        } else {
            drawTarget(corner, HX8357_YELLOW);
        }
        return;
    }
    int sx, sy;
    toScreen(rx, ry, sx, sy);
    Serial.printlnf("tap screen x=%d y=%d (raw %.0f %.0f)", sx, sy, rx, ry);
    tft.fillCircle(sx, sy, 5, HX8357_YELLOW);
}
