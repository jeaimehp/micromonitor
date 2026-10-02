# Xenon TFT System Dashboard

A Particle Xenon with an Adafruit 3.5" 480x320 TFT FeatherWing shows this Mac's CPU, RAM, disk and network
graphs and the top 5 processes, refreshed every 2 seconds.

```
Mac: host/collector.py (psutil) -> host/sender.py --USB serial, JSON line every 2s--> Xenon: firmware/dashboard
```

## Run
**µMonitor (menu bar app):** build it with `.venv/bin/python setup.py py2app`, then open `dist/µMonitor.app`
(or copy it to /Applications). It streams metrics, pictures and themes to the display. Its menu has:
- **Display:** View (Dashboard / Album / Mixed), Layout, Theme, Album Pictures (Photos / Motivation), Slideshow speed,
  Rotation (incl. portrait for the album), Mixed View photo side, Clock on Pictures, Next/Previous Picture, Calibrate Touch…
  These mirror the touch menu on the screen (tap the screen to open it); either one updates the other.
- **Pictures:** Add Pictures… (copies into `~/Pictures/µMonitor`), Choose Photos Folder…, Open Photos Folder
- **Timer:** 1–60 minute presets, Custom…, Pause/Resume, Cancel. **Stopwatch:** Start/Stop, Reset.
  Both show on the display (clock tile / album badge); a finished timer flashes TIME'S UP and posts a notification.
- Streaming on/off, CPU % in the menu bar, Start at Login, Quit. No Dock icon.

**Command line (alternative):** `./run.sh` (add `-v` to see device messages). Run only one of the two: they share the USB port.

The process table header shows **LIVE** (green dot) while data arrives and **NO HOST DATA** (red dot) after 6 seconds without it.

## Screen
Dashboard layouts: Quad, Stacked, Focus, Tiles (8 themes: Dark, Light, Retro Green, Amber, Ocean, Synthwave, Solarized, High Contrast).

| CPU/GPU % (0-100) | RAM % + used/total GB |
|---|---|
| **Disk**: read/write MB/s (autoscaled) + % used | **Net**: rx/tx KB/s (autoscaled) + total rate |

The top 5 processes by CPU are listed below the graphs, with a CPU bar (100% = one core), CPU% and MEM%. Each graph
covers about 3.7 minutes.

## Build / flash firmware
```bash
firmware/flash.sh firmware/dashboard   # cloud compile (Device OS 1.5.2) + USB flash; needs `particle login`
```

## Setup from scratch
- `python3 -m venv .venv && .venv/bin/pip install psutil pyserial`
- `brew install dfu-util imagesnap`, `npm i -g particle-cli`
- Device OS on the Xenon: `particle update --target 1.5.2` (the last release that supports the Xenon)

## Notes
- Processes owned by root (e.g. WindowServer) aren't visible to the collector unless it runs with sudo.
- Per-process CPU% is per core, so it can exceed 100.

See `pickupandgo.md` for implementation details and handoff notes.
