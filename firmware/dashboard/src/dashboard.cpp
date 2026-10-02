#include "Particle.h"
#include "Adafruit_HX8357.h"

SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);

// Adafruit 3.5" TFT FeatherWing on a Xenon: Feather pin 9 -> D4, 10 -> D5 (SD_CS 5 -> D2, touch CS 6 -> D3).
const int TFT_CS = D4;
const int TFT_DC = D5;
const int SD_CS = D2;
const int TS_CS = D3;

Adafruit_HX8357 tft(TFT_CS, TFT_DC);

void testPattern() {
    const uint16_t colors[] = {HX8357_RED, HX8357_GREEN, HX8357_BLUE, HX8357_YELLOW,
                               HX8357_CYAN, HX8357_MAGENTA, HX8357_WHITE, HX8357_BLACK};
    int w = tft.width() / 8;
    for (int i = 0; i < 8; i++) {
        tft.fillRect(i * w, 0, w, 160, colors[i]);
    }
    tft.fillRect(0, 160, tft.width(), 160, HX8357_BLACK);
    tft.drawRect(0, 0, tft.width(), tft.height(), HX8357_WHITE);
    tft.setTextColor(HX8357_WHITE);
    tft.setTextSize(4);
    tft.setCursor(20, 190);
    tft.print("XENON TFT OK");
    tft.setTextSize(2);
    tft.setCursor(20, 250);
    tft.printf("%dx%d  OS %s", tft.width(), tft.height(), System.version().c_str());
}

void setup() {
    Serial.begin(115200);
    // Keep other SPI devices on the wing deselected.
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(TS_CS, OUTPUT);
    digitalWrite(TS_CS, HIGH);

    tft.begin();
    tft.setRotation(1);  // landscape, 480x320
    uint32_t t0 = millis();
    testPattern();
    Serial.printlnf("test pattern drawn in %lu ms", (unsigned long)(millis() - t0));
}

void loop() {
    static uint32_t last = 0;
    if (millis() - last > 2000) {
        last = millis();
        Serial.printlnf("alive %lu", (unsigned long)(last / 1000));
    }
}
