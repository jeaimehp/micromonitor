# Xenon TFT System Dashboard

A Particle Xenon with an Adafruit 3.5" 480x320 TFT FeatherWing shows this Mac's CPU, RAM, disk and network
graphs and the top 5 processes, refreshed every 2 seconds.

```
Mac: host/collector.py (psutil) -> host/sender.py --USB serial, JSON line every 2s--> Xenon: firmware/dashboard
```

## Run
**µMonitor (menu bar app):** build it with `.venv/bin/python setup.py py2app`, then open `dist/µMonitor.app`
(or copy it to /Applications). It shows a status icon with the live CPU % and has this menu:
connection status, Streaming (pause/resume), Show CPU % in Menu Bar, Start at Login, Quit. It has no Dock icon.

**Command line (alternative):**
```bash
./run.sh            # stream to the first /dev/cu.usbmodem*; add -v to see device acks
```
Run only one of them at a time: only one program can use the serial port.
The header of the process table shows **LIVE** (green dot) while data is arriving. It shows **NO HOST DATA** (red dot)
after 6 seconds without samples.

Autostart at login: use µMonitor's "Start at Login" menu item. For the command-line version, use `tools/install-launchd.sh` (undo with `--uninstall`).

## Screen
| CPU % (0-100) | RAM % + used/total GB |
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
