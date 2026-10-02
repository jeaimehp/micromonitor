#include "app.h"

// Album view: full-screen pictures streamed from µMonitor, advancing on a timer or by tapping the edges.
const uint8_t SLIDE_SECONDS[NUM_SLIDE_OPTIONS] = {5, 10, 30};

static int albumIndex = 0;
static int albumCount = -1;         // -1 = unknown (nothing fetched yet), 0 = folder is empty
static uint32_t lastChange = 0;
static bool showingMessage = false;

bool portraitActive() {
    return settings.view == VIEW_ALBUM && settings.albumPortrait && !menuOpen;
}

static void drawMessage(const char *title, const char *line1, const char *line2) {
    int w = tft.width(), h = tft.height();
    renderRegion(0, 0, w, h, [&] {
        canvas.fillRect(0, 0, w, h, theme.separator);
        canvas.fillRoundRect(16, h / 2 - 60, w - 32, 120, 10, theme.surface);
        canvas.setTextSize(2);
        canvas.setTextColor(theme.text);
        printCentered(title, w / 2, h / 2 - 40);
        canvas.setTextSize(1);
        canvas.setTextColor(theme.text2);
        printCentered(line1, w / 2, h / 2 + 2);
        printCentered(line2, w / 2, h / 2 + 18);
    });
    showingMessage = true;
}

// Rounded clock badge in the bottom-right corner of the picture.
void drawClockBadge() {
    if (!settings.clockBadge || !clockValid() || showingMessage) return;
    const int bw = 132, bh = 44;
    int x = tft.width() - bw - 8, y = tft.height() - bh - 8;
    renderRegion(x, y, bw, bh, [&] {
        // The corners outside the rounded badge can't show the picture underneath (it isn't kept in RAM),
        // so the badge is drawn on a square plate in the separator color.
        canvas.fillRect(x, y, bw, bh, theme.separator);
        canvas.fillRoundRect(x, y, bw, bh, 8, theme.surface);
        char hm[12], date[16];
        strlcpy(hm, Time.format(Time.now(), "%l:%M %p").c_str(), sizeof(hm));
        formatDate(date, sizeof(date));
        char *t = hm;
        while (*t == ' ') t++;
        canvas.setTextSize(2);
        canvas.setTextColor(theme.text);
        printCentered(t, x + bw / 2, y + 7);
        canvas.setTextSize(1);
        canvas.setTextColor(theme.text2);
        printCentered(date, x + bw / 2, y + 29);
    });
}

static void showPicture() {
    if (stale) {
        drawMessage("ALBUM", "Pictures come from the \xE6Monitor app on the Mac.", "Start it to see the album.");
        albumCount = -1;
        lastChange = millis();
        return;
    }
    int count;
    int w = tft.width(), h = tft.height();
    bool ok = fetchPicture(settings.folder, albumIndex, 0, 0, w, h, count);
    albumCount = count;
    lastChange = millis();
    if (count == 0) {
        drawMessage(settings.folder == 0 ? "NO PHOTOS YET" : "NO PICTURES",
                    "Add pictures from the \xE6Monitor menu:", "Pictures > Add Pictures...");
        return;
    }
    if (!ok) {
        drawMessage("PICTURE FAILED", "The transfer from the Mac was interrupted.", "Trying again shortly.");
        return;
    }
    showingMessage = false;
    drawClockBadge();
}

void albumShow() {
    applyRotation();
    showPicture();
}

void albumStep(int delta) {
    if (albumCount > 0) albumIndex = (albumIndex + delta + albumCount) % albumCount;
    else albumIndex = max(0, albumIndex + delta);
    showPicture();
}

void albumResetIndex() {
    albumIndex = 0;
    albumCount = -1;
}

void albumTick() {
    if (settings.view != VIEW_ALBUM || menuOpen || uiBusy) return;
    uint32_t interval = (uint32_t)SLIDE_SECONDS[settings.slideIdx % NUM_SLIDE_OPTIONS] * 1000;
    // Retry soon after a message (e.g. the host just came back); otherwise follow the slideshow interval.
    if (millis() - lastChange >= (showingMessage ? 3000 : interval)) albumStep(1);
}

// Left third = previous, right third = next, middle = menu (returns false so the caller opens the menu).
bool albumTap(int x, int y) {
    int w = tft.width();
    if (x < w / 3) {
        albumStep(-1);
        return true;
    }
    if (x >= w * 2 / 3) {
        albumStep(1);
        return true;
    }
    return false;
}
