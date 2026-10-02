#include "app.h"

SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);

Metrics metrics;
uint32_t samples = 0;
uint32_t parseErrors = 0;
uint32_t lastSampleMs = 0;
bool stale = true;
bool uiBusy = false;               // a blocking screen (e.g. calibration) owns the display
const uint32_t STALE_MS = 6000;  // 3 missed samples

bool parseMetrics(const char *line, Metrics &m) {
    JSONValue root = JSONValue::parseCopy(line);
    if (!root.isObject()) {
        return false;
    }
    JSONObjectIterator it(root);
    while (it.next()) {
        JSONString key = it.name();
        JSONValue v = it.value();
        if (key == "c") m.cpu = v.toDouble();
        else if (key == "r") m.ram = v.toDouble();
        else if (key == "ru") m.ramUsed = v.toDouble();
        else if (key == "rt") m.ramTotal = v.toDouble();
        else if (key == "d") m.disk = v.toDouble();
        else if (key == "dr") m.diskRead = v.toDouble();
        else if (key == "dw") m.diskWrite = v.toDouble();
        else if (key == "nr") m.netRx = v.toDouble();
        else if (key == "nt") m.netTx = v.toDouble();
        else if (key == "p" && v.isArray()) {
            JSONArrayIterator procs(v);
            m.numProcs = 0;
            while (procs.next() && m.numProcs < NUM_PROCS) {
                JSONArrayIterator f(procs.value());
                Proc &p = m.procs[m.numProcs++];
                p.name[0] = 0;
                p.cpu = p.mem = 0;
                if (f.next()) strlcpy(p.name, (const char *)f.value().toString(), sizeof(p.name));
                if (f.next()) p.cpu = f.value().toDouble();
                if (f.next()) p.mem = f.value().toDouble();
            }
        }
    }
    return true;
}

// ---- Serial line reader ----
const size_t LINE_MAX = 1024;
char lineBuf[LINE_MAX];
size_t lineLen = 0;
bool lineOverflow = false;

// Returns true when a complete line is in lineBuf.
bool readLine() {
    while (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\r') continue;
        if (ch == '\n') {
            bool ok = !lineOverflow && lineLen > 0;
            lineBuf[lineLen] = 0;
            lineLen = 0;
            lineOverflow = false;
            if (ok) return true;
            continue;
        }
        if (lineLen < LINE_MAX - 1) lineBuf[lineLen++] = ch;
        else lineOverflow = true;
    }
    return false;
}

// ---- Main loop ----
void redrawView() {
    if (uiBusy) return;
    drawDashboard();
}

// Handle one complete line from the host: a JSON sample, or a test command ("tap X Y").
static void handleLine() {
    int tx, ty;
    if (sscanf(lineBuf, "tap %d %d", &tx, &ty) == 2) {
        injectTap(tx, ty);
        return;
    }
    if (!parseMetrics(lineBuf, metrics)) {
        parseErrors++;
        Serial.printlnf("err %lu", (unsigned long)parseErrors);
        return;
    }
    samples++;
    lastSampleMs = millis();
    stale = false;
    ingestSample();
    uint32_t t0 = millis();
    bool drawn = !menuOpen && !uiBusy;
    if (drawn) drawDashboard();
    Serial.printlnf("ack %lu c=%.1f p=%d draw=%lums free=%lu%s", (unsigned long)samples, metrics.cpu,
                    metrics.numProcs, (unsigned long)(millis() - t0), (unsigned long)System.freeMemory(),
                    drawn ? "" : " hidden");
}

void serviceSerial() {
    while (readLine()) handleLine();
}

void setup() {
    Serial.begin(115200);
    // Keep the other SPI devices on the wing deselected.
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(TS_CS, OUTPUT);
    digitalWrite(TS_CS, HIGH);

    loadSettings();
    gfxBegin();
    touchBegin();
    drawDashboard();
}

void loop() {
    serviceSerial();
    if (!stale && millis() - lastSampleMs > STALE_MS) {
        stale = true;
        if (!menuOpen && !uiBusy) drawDashboardStatus();
    }
    int x, y;
    if (pollTap(x, y)) {
        if (menuOpen) menuTap(x, y);
        else openMenu();
    }
    menuTick();
}
