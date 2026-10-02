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
| 2 | Display bring-up: HX8357 driver, pin map, test pattern (user confirms visually) | IN PROGRESS (flashed; awaiting visual check) |
| 3 | Host collector: metrics as JSON lines every 2s | DONE (done early, while blocked on login) |
| 4 | Serial link host->device, parse + ack, auto-reconnect | TODO |
| 5 | Dashboard frame + CPU graph | TODO |
| 6 | RAM graph | TODO |
| 7 | Disk graph (usage + I/O) | TODO |
| 8 | Network graph (rx/tx) | TODO |
| 9 | Top-5 process table | TODO |
| 10 | Polish: stale indicator, partial redraws, run.sh / optional launchd | TODO |

## Layout (480x320)
- A 2x2 grid of graph panels at the top, each about 240x105: CPU, RAM, Disk, Net
- A top-5 process table at the bottom, about 110px high

## Current status
Steps 0, 1 and 3 are complete. Step 2: firmware/dashboard holds the test pattern (8 color bars on the top half, plus "XENON TFT OK"
and "480x320 OS 1.5.2" text on the bottom half). It is flashed but NOT yet visually verified.
Webcam capture `imagesnap -d "HD Pro Webcam C920" -w 2 out.jpg` HUNG, most likely on the macOS camera permission prompt for the
terminal app. The user was asked to grant it. If the webcam keeps failing, ask the user to describe the screen.
Display verification: the user approved WEBCAM verification. A Logitech C920 ("HD Pro Webcam C920") is pointed at the screen.
Use imagesnap (brew) for this; there is also an OBSBOT camera, so select the device by name.
Do NOT read ~/.particle config files (the permission policy blocks reading credentials). The user is logged in to the Particle CLI.

## Device / firmware facts
- The Xenon now runs Device OS 1.5.2 (installed with `particle update --target 1.5.2`). Cloud compile for xenon@1.5.2 works.
- Build and flash any firmware project: `firmware/flash.sh firmware/<proj>` (cloud compile + `flash --local --application-only`).
  The device leaves DFU by itself after flashing. Its serial port is `/dev/cu.usbmodem2101` (it may change, so glob `/dev/cu.usbmodem*`).
  The port takes a few seconds to appear after flashing.
- Firmware uses SYSTEM_MODE(MANUAL) + SYSTEM_THREAD(ENABLED) (no mesh/cloud). The status LED may show listening/blue; that is expected.
- Display: library Adafruit_HX8357_RK 1.0.10 (pulls in Adafruit_GFX_RK, BusIO_RK, STMPE610_RK), declared in
  firmware/dashboard/project.properties so the cloud compiler fetches it. Constructor `Adafruit_HX8357 tft(D4, D5)`; setRotation(1) = 480x320.
  Feather->Xenon pin map: TFT_CS 9->D4, TFT_DC 10->D5, SD_CS 5->D2, TOUCH_CS 6->D3 (drive SD/touch CS HIGH).
  Pin map derived from header positions; NOT yet confirmed on hardware.
- firmware/hello: prints "hello N os=1.5.2" every 1s. Verified.

## Host collector protocol (host/collector.py)
- Test with `.venv/bin/python host/collector.py 3`, which prints 3 lines (one per 2s; 0 means run forever).
- JSON keys: c cpu%, r ram%, ru/rt ram used/total GB, d disk used% (/System/Volumes/Data), dr/dw disk MB/s,
  nr/nt net KB/s, p = 5 x [name(<=16 chars), cpu%, mem%]. A line is about 230 bytes.
- Per-process cpu% is per core (it can exceed 100). Root processes (e.g. WindowServer) are hidden without sudo.

## Log
- Step 1: Device OS 1.5.2 flashed, hello firmware heartbeat confirmed over USB serial.
- Step 3: collector.py verified: `yes` shows at 99.9% in the top-5, disk % matches df.
- Step 0: git init, .venv with psutil and pyserial, dfu-util via brew, particle-cli via npm. Xenon is visible in DFU.
