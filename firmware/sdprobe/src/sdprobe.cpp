#include "Particle.h"
#include "SdFat.h"

// SD-only probe: no TFT/touch traffic. Send "t <mhz>" to run 10 mount+list cycles at that clock.
SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);

const int TFT_CS = D4, SD_CS = D2, TS_CS = D3;
SdFat sd;

void test(int mhz) {
    int okMount = 0, okList = 0;
    for (int i = 0; i < 10; i++) {
        bool m = sd.begin(SD_CS, SD_SCK_MHZ(mhz));
        int entries = 0;
        if (m) {
            okMount++;
            FatFile dir, f;
            if (dir.open("/motivation", O_RDONLY)) {
                while (f.openNext(&dir, O_RDONLY)) {
                    entries++;
                    f.close();
                }
                dir.close();
            }
            if (entries >= 18) okList++;
        }
        Serial.printlnf("probe %dMHz %d mount=%d entries=%d err=0x%x", mhz, i, m, entries, sd.cardErrorCode());
    }
    Serial.printlnf("probe done %dMHz: mounted %d/10, full listing %d/10", mhz, okMount, okList);
}

void setup() {
    Serial.begin(115200);
    for (int pin : {TFT_CS, SD_CS, TS_CS}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        int mhz;
        if (sscanf(cmd.c_str(), "t %d", &mhz) == 1) test(mhz);
    }
}
