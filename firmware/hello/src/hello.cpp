#include "Particle.h"

// Bring-up firmware: no mesh/cloud, USB serial heartbeat, blink D7.
SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);

void setup() {
    Serial.begin(115200);
    pinMode(D7, OUTPUT);
}

void loop() {
    static uint32_t n = 0;
    digitalWrite(D7, (n & 1) ? HIGH : LOW);
    Serial.printlnf("hello %lu os=%s", (unsigned long)n++, System.version().c_str());
    delay(1000);
}
