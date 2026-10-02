#include "app.h"

SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);

// Larger USB serial receive buffer: with the default one the host can only push about 54 KB/s, which makes
// streamed pictures slow (a full screen is 300 KB). Device OS calls this hook at startup.
HAL_USB_USART_Config acquireUSBSerialBuffer() {
    static uint8_t rxBuf[4096];
    static uint8_t txBuf[512];
    HAL_USB_USART_Config conf = {};
    conf.size = sizeof(conf);
    conf.rx_buffer = rxBuf;
    conf.rx_buffer_size = sizeof(rxBuf);
    conf.tx_buffer = txBuf;
    conf.tx_buffer_size = sizeof(txBuf);
    return conf;
}

Metrics metrics;
uint32_t samples = 0;
uint32_t parseErrors = 0;
uint32_t lastSampleMs = 0;
uint32_t lastLiveMs = 0;  // last time the host was known to be alive (a sample or a completed transfer)
bool stale = true;
bool uiBusy = false;
struct PicTest {
    bool pending;
    int folder, n, w, h;
} pictest;  // serial "pictest F N W H": fetch and draw a picture at (0, 0)               // a blocking screen (e.g. calibration) owns the display
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
        else if (key == "g") m.gpu = v.toDouble();
        else if (key == "r") m.ram = v.toDouble();
        else if (key == "ru") m.ramUsed = v.toDouble();
        else if (key == "rt") m.ramTotal = v.toDouble();
        else if (key == "d") m.disk = v.toDouble();
        else if (key == "dr") m.diskRead = v.toDouble();
        else if (key == "dw") m.diskWrite = v.toDouble();
        else if (key == "nr") m.netRx = v.toDouble();
        else if (key == "nt") m.netTx = v.toDouble();
        else if (key == "t") m.time = v.toInt();
        else if (key == "tz") m.tzOffset = v.toInt();
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

// ---- Clock: set from the host's time, then kept by the Xenon's own RTC between samples ----
static void syncClock() {
    if (metrics.time <= 0) return;
    Time.zone(metrics.tzOffset / 3600.0f);
    if (!Time.isValid() || abs((long)(Time.now() - metrics.time)) > 2) Time.setTime(metrics.time);
}

bool clockValid() {
    return Time.isValid() && Time.year() >= 2024;
}

void formatClock(char *buf, size_t len) {
    if (!clockValid()) {
        buf[0] = 0;
        return;
    }
    // e.g. "Fri Oct 2  4:21 PM"
    strlcpy(buf, Time.format(Time.now(), "%a %b %e %l:%M %p").c_str(), len);
}

// ---- Main loop ----
void redrawView() {
    if (uiBusy) return;
    applyRotation();
    if (settings.view == VIEW_ALBUM) albumShow();
    else if (settings.view == VIEW_MIXED) mixedShow();
    else drawDashboard();
}

// Handle one complete line from the host: a JSON sample, or a test command ("tap X Y").
static void handleLine() {
    int tx, ty;
    if (sscanf(lineBuf, "tap %d %d", &tx, &ty) == 2) {
        injectTap(tx, ty);
        return;
    }
    if (!strncmp(lineBuf, "thm ", 4)) {
        handleThemeLine(lineBuf);
        return;
    }
    if (!strncmp(lineBuf, "pic ", 4)) {
        handlePictureHeader(lineBuf);
        return;
    }
    int benchBytes;
    if (sscanf(lineBuf, "rxbench %d", &benchBytes) == 1) {
        // Receive benchBytes raw bytes as fast as possible: tight available()/read() loop vs readBytes().
        uint32_t t0 = millis();
        int got = 0;
        uint8_t *buf = canvasBytes();
        while (got < benchBytes && millis() - t0 < 20000) {
            int avail = Serial.available();
            while (avail-- > 0 && got < benchBytes) buf[got++ % (CANVAS_PIXELS * 2)] = Serial.read();
        }
        uint32_t dt = millis() - t0;
        Serial.printlnf("rxbench %d bytes in %lums = %lu KB/s", got, (unsigned long)dt,
                        (unsigned long)(dt ? got / dt : 0));
        return;
    }
    int f, n, w, h;
    if (sscanf(lineBuf, "pictest %d %d %d %d", &f, &n, &w, &h) == 4) {
        pictest = {true, f, n, w, h};
        return;
    }
    if (!parseMetrics(lineBuf, metrics)) {
        parseErrors++;
        Serial.printlnf("err %lu", (unsigned long)parseErrors);
        return;
    }
    syncClock();
    samples++;
    lastSampleMs = millis();
    // Fetch µMonitor's themes on the first sample, and again after the host was really gone (it may have
    // restarted with different themes). Short gaps (e.g. during a picture transfer) don't count.
    if (samples == 1 || (stale && millis() - lastLiveMs > 30000)) requestThemes();
    lastLiveMs = millis();
    stale = false;
    ingestSample();
    uint32_t t0 = millis();
    bool drawn = !menuOpen && !uiBusy && settings.view != VIEW_ALBUM;
    if (drawn) {
        if (settings.view == VIEW_MIXED) drawMixedPanels();
        else drawDashboard();
    }
    Serial.printlnf("ack %lu c=%.1f p=%d draw=%lums free=%lu%s", (unsigned long)samples, metrics.cpu,
                    metrics.numProcs, (unsigned long)(millis() - t0), (unsigned long)System.freeMemory(),
                    drawn ? "" : " hidden");
}

void serviceSerial() {
    // Stop right after a picture header: the binary payload that follows is read by fetchPicture().
    while (!picturePending() && readLine()) handleLine();
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
    applyTheme(settings.themeIdx);  // falls back to Dark until µMonitor sends its themes
    touchBegin();
    drawDashboard();
}

void loop() {
    serviceSerial();
    if (pictest.pending) {
        pictest.pending = false;
        int count;
        fetchPicture(pictest.folder, pictest.n, 0, 0, pictest.w, pictest.h, count);
    }
    if (!stale && millis() - lastSampleMs > STALE_MS) {
        stale = true;
        if (!menuOpen && !uiBusy) {
            if (settings.view == VIEW_DASHBOARD) drawDashboardStatus();
            else if (settings.view == VIEW_MIXED) drawMixedPanels();
        }
    }
    // Keep the clock current even when no samples arrive.
    static int lastMinute = -1;
    if (clockValid() && Time.minute() != lastMinute) {
        lastMinute = Time.minute();
        if (!menuOpen && !uiBusy) {
            if (settings.view == VIEW_ALBUM) drawClockBadge();
            else if (settings.view == VIEW_MIXED) drawMixedPanels();
            else drawDashboardStatus();
        }
    }
    albumTick();
    mixedTick();
    int x, y;
    if (pollTap(x, y)) {
        if (menuOpen) menuTap(x, y);
        else {
            bool handled = settings.view == VIEW_ALBUM   ? albumTap(x, y)
                           : settings.view == VIEW_MIXED ? mixedTap(x, y)
                                                         : dashboardTap(x, y);
            if (!handled) openMenu();
        }
    }
    menuTick();
}
