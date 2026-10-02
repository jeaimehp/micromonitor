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
| 10 | Polish: stale indicator, partial redraws, run.sh / optional launchd | TODO |

## Layout (480x320)
- A 2x2 grid of graph panels at the top, each about 240x105: CPU, RAM, Disk, Net
- A top-5 process table at the bottom, about 110px high

## Current status
Steps 0-9 are complete. Next: step 10 (polish: stale-data indicator, run.sh launcher, optional launchd autostart, long soak test).
Visual verification: `tools/snap.sh <scratch>/x.jpg`, then view the image. The webcam permission is granted, and the C920 faces the TFT.
After flashing, wait a few seconds for the reboot and redraw before taking a photo (otherwise it can catch a partial redraw).
Do NOT read ~/.particle config files (the permission policy blocks reading credentials). The user is logged in to the Particle CLI.

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
