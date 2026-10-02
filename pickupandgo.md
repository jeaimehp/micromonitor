# Pick Up And Go — Xenon TFT System Dashboard

Handoff file. Any agent should be able to resume from the **Current status** section.
Keep this file updated at the end of every step, and commit it with that step.

## Goal
A Particle Xenon (nRF52840) with an Adafruit 3.5" 480x320 TFT FeatherWing (HX8357D, SPI) shows a live
dashboard of this Mac's resources, refreshed every 2s:
- Graphs: CPU %, RAM %, Disk (usage + read/write throughput), Network (rx/tx throughput)
- Table: top 5 processes (name, CPU%, MEM%)

## Architecture
```
Mac: host/collector.py (psutil) --USB CDC serial, 1 line / 2s--> Xenon firmware --> HX8357D TFT
```
The Mac collects the metrics, and the Xenon only parses and draws. The SD card is NOT needed. Ask the user
to insert it only if a future feature needs it, and wait for them to confirm.

## Environment facts
- macOS 26 (arm64). Project: `/Users/jeaimehp/Documents/xenon-feather-tft`
- Python venv: `.venv/` (psutil 7.2.2, pyserial 3.5). Run with `.venv/bin/python`.
- dfu-util 0.11 (Homebrew).
- particle-cli 3.51.0 at `/Users/jeaimehp/.hermes/node/bin/particle` (NOT on PATH, so use the full path).
- Xenon DFU USB id `2b04:d00e`, serial `<device-serial>`.
- Xenon's last supported Device OS is **1.5.2**. Plan: cloud compile with `--target 1.5.2`.
  Fallback: local build from device-os v1.5.2 source.

## Plan / steps
Each step ends with: test, git commit, update this file.

| # | Step | Status |
|---|------|--------|
| 0 | Project setup: git, venv, tools | DONE |
| 1 | Device bring-up: Device OS 1.5.2, hello firmware, USB serial heartbeat | DONE |
| 2 | Display bring-up: HX8357 driver, pin map, test pattern (user confirms visually) | DONE (verified by webcam) |
| 3 | Host collector: metrics as JSON lines every 2s | DONE (done early, while blocked on login) |
| 4 | Serial link host->device, parse + ack, auto-reconnect | DONE |
| 5 | Dashboard frame + CPU graph | DONE |
| 6 | RAM graph | DONE |
| 7 | Disk graph (usage + I/O) | DONE |
| 8 | Network graph (rx/tx) | DONE |
| 9 | Top-5 process table | DONE |
| 10 | Polish: stale indicator, partial redraws, run.sh / optional launchd | DONE |
| 11 | macOS menu bar app "µMonitor" (user request): rumps + py2app, status icon | DONE (menu UI awaiting user visual check) |
| 12 | Touch bring-up: detect STMPE610 (SPI, CS D3) vs TSC2007 (I2C 0x48), raw readings, calibration screen | DONE (awaiting user accuracy confirmation) |
| 13 | Touch menu overlay + persisted settings (EEPROM) + rotation | DONE |
| 14 | Themes + layouts in firmware (built-in default; layouts Quad/Stacked/Focus/Tiles; mixed photo left/right) | DONE (mixed side comes with step 19) |
| 15 | Mac tool: generate pixel-art motivation set, theme files, convert photos -> repo `sdcard/` | TODO |
| 16 | SD card: ASK the user to insert it in the Mac, copy files, eject, ask them to move it to the FeatherWing | TODO |
| 17 | Firmware SD support: list folders, load themes from SD | TODO |
| 18 | Album view: slideshow, tap left/right edge = prev/next, middle = menu | TODO |
| 19 | Mixed view: half-size photo + compact graphs | TODO |
| 20 | Soak test with view switching, docs | TODO |

## Phase 2 spec (steps 12-20, approved by the user)
- Touch: tapping opens a full-screen menu with big buttons (>=60px). Tap outside or wait 10s to close.
  Menu items: View (Dashboard/Album/Mixed), Album folder (/photos | /motivation), slideshow speed (5/10/30s),
  Theme (cycle through SD themes, with a built-in dark fallback), Layout (separate from the theme), Rotate, Recalibrate touch.
- Rotation decision (the user approved the recommended default): a 180 deg flip for ALL views, plus portrait 90/270 for the ALBUM view only.
- Layouts: Quad (current 2x2 + top-5), Stacked (4 full-width strips with about 8 min of history + top-3), Focus (one big graph chosen by tap,
  3 small tiles + top-5), Tiles (big numbers + sparklines + top-5). Mixed view: photo left / photo right.
- Theme files /themes/*.thm (key=value): name, surface, grid, text, text2, series1, series2, good, critical, layout.
  Examples: Dark/Quad, Light/Quad, Retro Green/Stacked, Amber/Tiles, Ocean/Focus, Synthwave/Tiles, Solarized/Stacked, High Contrast/Tiles.
  Validate each series pair with the dataviz skill's validate_palette.js against that theme's surface.
- Images: raw 480x320 RGB565 big-endian files (.565) made by a Mac converter; mixed view downsamples 2x on load.
  SD folders: /photos (the user's photos; samples for now, since the user gave no folder), /motivation (generated pixel art + captions), /themes.
- SD format: if the card is exFAT, ASK the user before reformatting to FAT32 (that erases the card). Not yet confirmed.
- Settings persist in EEPROM (they work without SD). The touch calibration is also stored in EEPROM.

## Layout (480x320)
- A 2x2 grid of graph panels at the top, each about 240x105: CPU, RAM, Disk, Net
- A top-5 process table at the bottom, about 110px high

## Current status
Steps 0-11 are complete. Step 12 is done: the touch controller is an STMPE610 (ver 0x0811, SPI, CS D3) and works with Adafruit_STMPE610_RK.
firmware/touchtest = guided 4-corner calibration (targets inset 30px, rotation 3), then a draw mode. Measured calibration (rotation 3):
swap=1 (raw y -> screen x), uL=3562 uR=286 (raw y at x=30 / x=449), vT=516 vB=3537 (raw x at y=30 / y=289).
Use these as the firmware DEFAULT; the menu's "Calibrate touch" item (requested by the user) reruns the guided screen and saves it to EEPROM.
For rotation 1 (flipped 180), mirror both axes: sx' = 479 - sx, sy' = 319 - sy.
Step 13 is done (the user confirmed touch accuracy). Step 14 is done: themes + layouts. NEXT: step 15 (generate the pixel-art set +
photo converter into sdcard/). The theme files are already generated in sdcard/themes by tools/make_themes.py.
µMonitor was restarted after step 13.
(Earlier notes:) Steps 0-9 are complete.
Step 10: stale indicator (LIVE / NO HOST DATA / WAITING FOR HOST in the table header) is done and verified by webcam; run.sh, README.md and
tools/install-launchd.sh (NOT installed; it changes login items, so ask the user) are written. 12-min soak PASSED: 360/360 acks, 0 errors,
free=28328 constant, draw 213-223ms.
Step 11 (user asked mid-step-10): turn run.sh into a menu bar status-icon app, named "µMonitor" (MICRO SIGN U+00B5, per the user;
bundle id / LaunchAgent label com.xenon-feather-tft.micromonitor). host/menubar.py (rumps), host/sender.py refactored
into a `Streamer` class (thread-safe stop), template icon from tools/make_icon.py, setup.py (py2app, LSUIElement).
`dist/µMonitor.app` builds (29MB) and runs: it holds the port and the TFT shows LIVE with all panels (webcam-verified).
The port is opened with exclusive=True, so a second sender gets "Could not exclusively lock port" and retries (verified); the menu then shows
"Xenon port busy". The menu bar can't be screenshotted (no Screen Recording permission for the terminal), so the user must
check the icon and menu visually. Rebuild: `rm -rf build dist && .venv/bin/python setup.py py2app`.
"Start at Login" writes ~/Library/LaunchAgents/com.xenon-feather-tft.micromonitor.plist (RunAtLoad, no KeepAlive, so Quit stays quit).
It points at the app's location at the time, so re-toggle it if the app moves (e.g. to /Applications).

## Device / firmware facts
- The Xenon now runs Device OS 1.5.2 (installed with `particle update --target 1.5.2`). Cloud compile for xenon@1.5.2 works.
- Build and flash any firmware project: `firmware/flash.sh firmware/<proj>` (cloud compile + `flash --local --application-only`).
  The device leaves DFU by itself after flashing. Its serial port is `/dev/cu.usbmodem2101` (it may change, so glob `/dev/cu.usbmodem*`).
  The port takes a few seconds to appear after flashing.
- Firmware uses SYSTEM_MODE(MANUAL) + SYSTEM_THREAD(ENABLED) (no mesh/cloud). The status LED may show listening/blue; that is expected.
- Display: library Adafruit_HX8357_RK 1.0.10 (pulls in Adafruit_GFX_RK, BusIO_RK, STMPE610_RK), declared in
  firmware/dashboard/project.properties so the cloud compiler fetches it. Constructor `Adafruit_HX8357 tft(D4, D5)`; setRotation(3) = 480x320 upright as mounted (rotation 1 shows upside down).
  Feather->Xenon pin map: TFT_CS 9->D4, TFT_DC 10->D5, SD_CS 5->D2, TOUCH_CS 6->D3 (drive SD/touch CS HIGH).
  Pin map CONFIRMED on hardware.
- firmware/hello: prints "hello N os=1.5.2" every 1s. Verified.

## Firmware structure (firmware/dashboard/src, since step 13)
- app.h: shared declarations (pins, Metrics, Canvas, Theme, Settings, touch/menu/view APIs).
- main.cpp: serial line reader + JSON parse, handleLine() (also the test command "tap X Y"), setup/loop, redrawView().
  `uiBusy` = a blocking screen owns the display. Samples are always ingested but drawn only if !menuOpen && !uiBusy
  (the ack then ends with " hidden").
- gfx.cpp: tft, Canvas (bufW/bufH buffer + origin offset), the built-in theme "Dark", printRight/printCentered, renderStrips(draw)
  (renders a full-screen draw function in 480x40 strips. setOrigin() widens the logical size to 480x320 so GFX text isn't clipped).
- dashboard_view.cpp: panels, history, drawPanel/drawTable, ingestSample(), drawDashboard(), drawDashboardStatus().
- touch.cpp: STMPE610, readRawPress (averages one press and calls serviceSerial while held), rawToScreen (calibration + flip),
  pollTap, injectTap, runCalibration (guided 4 corners, aborts after 30s without a press).
- settings.cpp: Settings struct in EEPROM at 0 (magic "XMON", version 1). Bump SETTINGS_VERSION when the struct changes (resets to defaults).
- menu.cpp: BUTTONS[] geometry (header with CLOSE; rows y 52/118/184/250, h 56), describe() = caption/value/enabled per button,
  menuTap() actions, 10s auto-close. Album/Mixed/Theme/Layout/Folder/Slides are DISABLED placeholders until their steps.
- Menu button centers for tap tests: Dashboard (83,80) Album (240,80) Mixed (397,80) Theme (122,146) Layout (358,146)
  Folder (122,212) Slides (358,212) Rotate (122,278) Calibrate (358,278) Close (420,25). Any tap opens the menu.
- tools/uitest.py "tap X Y, wait S, snap NAME, ..." --snapdir D: streams real samples and drives the UI. Use it for UI tests
  (stop µMonitor first: `pkill -f "MacOS/µMonitor"`. Restart it with `open "dist/µMonitor.app"`).

## Themes + layouts (step 14)
- tools/make_themes.py is the SINGLE SOURCE for all 8 themes. It writes sdcard/themes/NN_slug.thm and
  firmware/dashboard/src/builtin_themes.h (Dark + Light compiled in). It checks WCAG contrast; series pairs were validated with
  the dataviz validate_palette.js (all pass). Re-run both if colors change. .thm format: name=, layout=quad|stacked|focus|tiles,
  then surface grid text text2 series1 series2 good critical separator button accent = #rrggbb.
- Firmware: ThemeSpec (RGB888) -> applyTheme(i) -> `theme` (RGB565). themeCount()/themeName() cover the built-ins now;
  step 17 appends the SD themes. Menu Theme = next theme AND switch to its suggested layout; Layout cycles independently.
- Settings v2 adds `focus`. Layouts live in dashboard_view.cpp: Quad (2x2 240x105 + table at y210 with 5 rows), Stacked (4 strips
  480x58 + table at y232 with 3 rows of 24px), Focus (main 480x150 + 3 mini tiles 160x60 at y150; tap a tile to focus it via
  dashboardTap), Tiles (2x2 with size-4 numbers + sparklines). HISTORY=240 samples. renderRegion(x,y,w,h,lambda) strips any
  region to fit the canvas.
- Free RAM after step 14: about 24.0 KB.

## Rendering approach (IMPORTANT for performance)
- Drawing directly with tft.* GFX calls is extremely slow (about 3s for a few lines of text): each pixel or char is a separate SPI call.
- Instead, draw into `Canvas` (a custom Adafruit_GFX subclass over the shared `canvasBuf`, 240*110 px = 52.8KB), call
  `canvas.resize(w,h)` (requires w*h <= CANVAS_PIXELS), then `canvas.push(x,y)`, which byte-swaps the buffer and sends it with one
  `SPI.transfer` DMA. After a push the buffer is swapped, so always redraw before pushing again.
- SPI runs at 32 MHz with no visible glitches. Pushing about 480x280 px takes about 185 ms in total.
- RAM: about 30 KB free after the canvas (shown as `free` on the status screen).

## Dashboard code structure (firmware/dashboard/src/dashboard.cpp)
- `Panel panels[4]` = CPU (0,0), RAM (240,0), DISK (0,105), NET (240,105), each 240x105. numSeries 0 = "pending" placeholder.
  fixedMax 100 for %; 0 = autoscale (floor 1.0). Ring history of 113 samples, 2px per sample, newest on the right.
- `pushSample(panel, a, b)` + set `panel.value` (headline text, top right) inside `updateDashboard()`, which redraws all 4 panels.
- drawTable(): y 210..320, a 15px header strip + 5 x 19px rows. Columns: name (size 2, <=16 chars), CPU bar x208 w110
  (scaled to 100% = one core, clipped), CPU% right-aligned at 400, MEM% right-aligned at 472. A full redraw incl. table takes about 213 ms.
- Theme (from the dataviz skill's validated palette, dark mode): surface #1a1a19, grid #383835, text #fff / #c3c2b7,
  series 1 blue #3987e5, series 2 orange #d95926 (this pair passes the CVD and contrast validator). 2-series panels need a legend
  (colored swatch + text label in the text color, never colored text).
- Graph area y 36..98. Legend row at y 25 (size-1 text): for 2-series panels, swatch + "label value" (no decimals at >=10);
  for autoscale panels, "max N unit" right-aligned. niceCeil() rounds the autoscale max to 1/2/5 x 10^n (min 1).
- A full 4-panel redraw takes about 136 ms.

## Running
- `.venv/bin/python host/sender.py -v [--count N] [--port P]` collects every 2s and writes JSON lines to the first `/dev/cu.usbmodem*`.
  It prints device replies (`ack N c=.. p=.. draw=..ms`, or `err N`) and reconnects on its own (tested with `particle usb reset`).

## Testing helpers
- `tools/snap.sh out.jpg`: webcam photo (1000px). For detail, take a full-res photo with
  `imagesnap -d "HD Pro Webcam C920" -w 2 full.jpg` and crop it with `sips -c 330 800 --cropOffset 560 600` (that crop covers the top panels).
  sips crop args: `-c <height> <width> --cropOffset <y> <x>`. Panels in the 1920x1080 photo: DISK about y640 x600, NET about y640 x900.
- `tools/synthetic.py 'r=25*20,r=50*20' [key=val ...]` sends synthetic samples quickly (0.2s apart) to check geometry. Keys follow the protocol below.
- Webcam autofocus drifts; if a photo is blurry, take another (-w 3).
- Real load: `yes` processes for CPU; a touched bytearray for RAM (macOS compresses it, so 10GB shows as about 4GB).

## Host collector protocol (host/collector.py)
- Test with `.venv/bin/python host/collector.py 3`, which prints 3 lines (one per 2s; 0 means run forever).
- JSON keys: c cpu%, r ram%, ru/rt ram used/total GB, d disk used% (/System/Volumes/Data), dr/dw disk MB/s,
  nr/nt net KB/s, p = 5 x [name(<=16 chars), cpu%, mem%]. A line is about 230 bytes.
- Per-process cpu% is per core (it can exceed 100). Root processes (e.g. WindowServer) are hidden without sudo.

## Log
- Step 14: 8 validated themes (Dark/Light built in), 4 layouts, Focus tile tap. All layouts + Light verified by webcam.
- Step 13: firmware split into modules; touch menu (rotate, calibrate, close, timeout), EEPROM settings (flip survives reboot),
  serial tap injection + tools/uitest.py. All verified by webcam.
- Step 12: STMPE610 found; guided calibration measured; taps plotted.
- Step 11: µMonitor menu bar app (host/menubar.py, setup.py, tools/make_icon.py); sender.py refactored into a Streamer class.
- Step 10: stale indicator, run.sh, README, launchd script, ack now includes free=. 12-minute soak passed.
- Step 9: process table; verified with 2x `yes` (both 100.0 at top, matching the sender's JSON). Host now ASCII-sanitizes names.
- Step 8: net panel (rx/tx KB/s, headline = total in KB/s or MB/s). Verified with a 1GB download from proof.ovh.net (about 38 MB/s)
  and an upload to speed.cloudflare.com/__up (about 23 MB/s). Cloudflare __down refuses curl, so don't use it.
- Step 7: disk panel (rd/wr MB/s + "N% used"). Real 4GB F_NOCACHE write/read showed spikes (about 2.1 GB/s); worst-case legend width checked with synthetic data.
- Step 6: RAM graph, value "used/totalG pct%"; scaling verified with synthetic 25/50/75/100 steps.
- Step 5: panel framework + CPU graph; verified with an 8x `yes` burst (about 50% plateau on 18 cores) by webcam.
- Step 4: JSON parse (Device OS JSONValue) + ack. Switched to canvas+DMA rendering (2945ms -> 185ms). Reconnect tested.
- Step 2: test pattern verified by webcam; switched to rotation 3.
- Step 1: Device OS 1.5.2 flashed, hello firmware heartbeat confirmed over USB serial.
- Step 3: collector.py verified: `yes` shows at 99.9% in the top-5, disk % matches df.
- Step 0: git init, .venv with psutil and pyserial, dfu-util via brew, particle-cli via npm. Xenon is visible in DFU.
