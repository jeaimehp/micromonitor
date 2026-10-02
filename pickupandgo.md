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
| 1 | Device bring-up: Device OS 1.5.2, hello firmware, USB serial heartbeat | TODO |
| 2 | Display bring-up: HX8357 driver, pin map, test pattern (user confirms visually) | TODO |
| 3 | Host collector: metrics as JSON lines every 2s | TODO |
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
Step 0 complete. Next: step 1. The user needs to run `particle login` (suggest `! /Users/jeaimehp/.hermes/node/bin/particle login`).
Still undecided: whether to verify the display by webcam (imagesnap) or have the user describe it. Ask at step 2.

## Log
- Step 0: git init, .venv with psutil and pyserial, dfu-util via brew, particle-cli via npm. Xenon is visible in DFU.
