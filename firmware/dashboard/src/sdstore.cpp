#include "app.h"
#include "SdFat.h"

// SD card on the FeatherWing (CS D2): theme files and picture folders.
static SdFat sd;
static bool sdOk = false;
static uint32_t lastAttempt = 0;
static const uint32_t RETRY_MS = 5000;

const char *const FOLDERS[NUM_FOLDERS] = {"/photos", "/motivation"};

static ThemeSpec sdThemes[MAX_SD_THEMES];
static int numSdThemes = 0;

static const char *const THEME_KEYS[TC_COUNT] = {"surface", "grid",     "text",      "text2",  "series1", "series2",
                                                  "good",    "critical", "separator", "button", "accent"};

static bool endsWith(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcasecmp(s + n - m, suffix) == 0;
}

// Parse one "key=value" theme file. Missing colors keep the built-in Dark values.
static bool parseTheme(FatFile &f, ThemeSpec &t) {
    t = BUILTIN_DEFAULT_SPEC();
    t.name[0] = 0;
    char line[64];
    while (f.fgets(line, sizeof(line)) > 0) {
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq) continue;
        *eq = 0;
        char *val = eq + 1;
        val[strcspn(val, "\r\n")] = 0;
        if (!strcmp(line, "name")) {
            strlcpy(t.name, val, sizeof(t.name));
        } else if (!strcmp(line, "layout")) {
            for (int i = 0; i < LAYOUT_COUNT; i++)
                if (!strcasecmp(val, LAYOUT_NAMES[i])) t.layout = i;
        } else {
            for (int i = 0; i < TC_COUNT; i++)
                if (!strcmp(line, THEME_KEYS[i])) t.colors[i] = strtoul(val[0] == '#' ? val + 1 : val, NULL, 16);
        }
    }
    return t.name[0] != 0;
}

static void loadThemes() {
    numSdThemes = 0;
    FatFile dir, f;
    if (!dir.open("/themes", O_RDONLY)) return;
    // Directory order is creation order; the files are copied in name order (NN_name.thm).
    while (numSdThemes < MAX_SD_THEMES && f.openNext(&dir, O_RDONLY)) {
        char name[40];
        f.getName(name, sizeof(name));
        ThemeSpec &t = sdThemes[numSdThemes];
        if (!f.isDir() && name[0] != '.' && endsWith(name, ".thm") && parseTheme(f, t) && !builtinThemeNamed(t.name))
            numSdThemes++;
        f.close();
    }
    dir.close();
}

static bool mount() {
    lastAttempt = millis();
    sdOk = sd.begin(SD_CS, SD_SCK_MHZ(16));
    if (sdOk) {
        loadThemes();
        Serial.printlnf("sd ok, %d themes", numSdThemes);
    }
    return sdOk;
}

bool sdBegin() {
    return mount();
}

bool sdReady() {
    return sdOk;
}

bool sdPoll() {
    if (sdOk || millis() - lastAttempt < RETRY_MS) return false;
    return mount();
}

int sdThemeCount() {
    return numSdThemes;
}

const ThemeSpec &sdTheme(int i) {
    return sdThemes[i];
}

// Name of the n-th picture (base name without the _L/_P.565 suffix) in a folder, for one orientation.
bool pictureName(int folder, int n, bool portrait, char *out, size_t len) {
    if (!sdOk) return false;
    FatFile dir, f;
    if (!dir.open(FOLDERS[folder], O_RDONLY)) return false;
    const char *suffix = portrait ? "_P.565" : "_L.565";
    int k = 0;
    bool found = false;
    while (f.openNext(&dir, O_RDONLY)) {
        char name[40];
        f.getName(name, sizeof(name));
        f.close();
        if (name[0] == '.' || !endsWith(name, suffix)) continue;
        if (k++ == n) {
            strlcpy(out, name, len);
            found = true;
            break;
        }
    }
    dir.close();
    return found;
}

int pictureCount(int folder, bool portrait) {
    char name[40];
    int n = 0;
    while (pictureName(folder, n, portrait, name, sizeof(name))) n++;
    return n;
}

bool openPicture(int folder, const char *name, FatFile &f) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", FOLDERS[folder], name);
    return f.open(path, O_RDONLY);
}

void sdInfo() {
    Serial.printlnf("sd %s, %d sd themes", sdOk ? "ok" : "missing", numSdThemes);
    for (int i = 0; i < numSdThemes; i++)
        Serial.printlnf("  theme %s layout %s", sdThemes[i].name, LAYOUT_NAMES[sdThemes[i].layout]);
    for (int f = 0; f < NUM_FOLDERS; f++)
        Serial.printlnf("  %s: %d landscape, %d portrait", FOLDERS[f], pictureCount(f, false), pictureCount(f, true));
}
