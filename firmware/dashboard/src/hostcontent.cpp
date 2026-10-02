#include "app.h"

// Themes and pictures streamed from µMonitor on request (see host/content.py and the Streamer in host/sender.py).
const char *const FOLDER_NAMES[NUM_FOLDERS] = {"Photos", "Motivation"};

static ThemeSpec hostThemes[MAX_HOST_THEMES];
static int numHostThemes = 0;
static int receivingThemes = 0;  // themes collected so far while a "thm" list is arriving

// Header of the picture currently arriving ("pic <count> <w> <h>"); the payload follows on the serial port.
static bool picPending = false;
static int picCount, picW, picH;

void requestThemes() {
    receivingThemes = 0;
    Serial.println("req themes");
}

int hostThemeCount() {
    return numHostThemes;
}

const ThemeSpec &hostTheme(int i) {
    return hostThemes[i];
}

// "thm <name>|<layout>|<c1>,...,<c11>" or "thm end".
void handleThemeLine(const char *line) {
    const char *p = line + 4;
    if (!strcmp(p, "end")) {
        numHostThemes = receivingThemes;
        Serial.printlnf("themes %d", numHostThemes);
        reportState();
        applyTheme(settings.themeIdx);  // the saved theme may be one of these
        if (!menuOpen && !uiBusy) redrawView();
        return;
    }
    if (receivingThemes >= MAX_HOST_THEMES) return;
    ThemeSpec t = BUILTIN_DEFAULT_SPEC();
    const char *bar = strchr(p, '|');
    if (!bar) return;
    strlcpy(t.name, p, min((size_t)(bar - p + 1), sizeof(t.name)));
    const char *bar2 = strchr(bar + 1, '|');
    if (!bar2) return;
    for (int i = 0; i < LAYOUT_COUNT; i++)
        if (!strncasecmp(bar + 1, LAYOUT_NAMES[i], bar2 - bar - 1) && (int)strlen(LAYOUT_NAMES[i]) == bar2 - bar - 1)
            t.layout = i;
    const char *c = bar2 + 1;
    for (int i = 0; i < TC_COUNT && *c; i++) {
        char *end;
        t.colors[i] = strtoul(c, &end, 16);
        c = *end == ',' ? end + 1 : end;
    }
    if (!builtinThemeNamed(t.name)) hostThemes[receivingThemes++] = t;
}

void handlePictureHeader(const char *line) {
    if (sscanf(line, "pic %d %d %d", &picCount, &picW, &picH) == 3) picPending = true;
}

bool picturePending() {
    return picPending;
}

// Next payload byte, or -1 after 2s without data. Serial.read() costs about 9us per call on Device OS 1.5.2, which
// caps the link at about 116 KB/s, hence the RLE.
static int nextByte() {
    uint32_t t0 = millis();
    while (!Serial.available())
        if (millis() - t0 > 2000) return -1;
    return Serial.read();
}

// Decode the RLE payload (see host/content.py rle_encode) into canvas strips and push each full strip.
static bool receiveRle(int x, int y, int w, int h) {
    uint8_t *buf = canvasBytes();
    const size_t stripPx = (size_t)min(h, (int)(CANVAS_PIXELS / w)) * w;
    const size_t total = (size_t)w * h;
    size_t done = 0, filled = 0;
    int row = 0;
    auto put = [&](uint8_t hi, uint8_t lo) {
        buf[2 * filled] = hi;
        buf[2 * filled + 1] = lo;
        filled++;
        done++;
        if (filled == stripPx || done == total) {
            int rows = filled / w;
            pushRaw(x, y + row, w, rows);
            row += rows;
            filled = 0;
        }
    };
    while (done < total) {
        int c = nextByte();
        if (c < 0) return false;
        if (c < 128) {
            for (int i = 0; i <= c && done < total; i++) {
                int hi = nextByte(), lo = nextByte();
                if (lo < 0) return false;
                put(hi, lo);
            }
        } else {
            int hi = nextByte(), lo = nextByte();
            if (lo < 0) return false;
            for (int i = 0; i < c - 126 && done < total; i++) put(hi, lo);
        }
    }
    return true;
}

bool fetchPicture(int folder, int n, int x, int y, int w, int h, int &count) {
    count = 0;
    bool wasBusy = uiBusy;
    uiBusy = true;  // samples that arrive meanwhile are ingested but not drawn
    picPending = false;
    Serial.printlnf("req pic %d %d %d %d", folder, n, w, h);
    uint32_t start = millis();
    while (!picPending && millis() - start < 4000) serviceSerial();
    bool ok = false;
    if (picPending) {
        picPending = false;
        count = picCount;
        if (picCount > 0 && picW == w && picH == h) ok = receiveRle(x, y, w, h);
    }
    uiBusy = wasBusy;
    if (ok) {
        lastSampleMs = millis();  // the host is clearly alive; don't flag it stale because of the transfer
    }
    Serial.printlnf("pic %s folder=%d n=%d count=%d %lums", ok ? "ok" : "fail", folder, n, count,
                    (unsigned long)(millis() - start));
    return ok;
}
