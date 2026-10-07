---
layout: default
title: Documentation
permalink: /documentation/
description: Parts, assembly, installation and usage for µMonitor.
---

# Documentation

* TOC
{:toc}

## Overview

µMonitor has two halves:

- **Firmware** (`firmware/dashboard`) runs on a Particle Xenon plugged into an Adafruit 3.5" 480×320 TFT FeatherWing.
  It draws the dashboard, album, menu and timer, and handles touch.
- **The µMonitor menu bar app** (`host/`) runs on your Mac. It samples CPU, GPU, memory, disk and network every
  2 seconds and streams them over USB as JSON lines. It also sends pictures and themes, and mirrors the display's menu.

```
Mac: µMonitor (host/menubar.py)                                     Xenon + 3.5" TFT FeatherWing
  collector.py  psutil + ioreg (GPU) ── JSON line every 2s ──────▶  firmware/dashboard
  timers.py     timer / stopwatch    ── "tm" inside each sample ─▶    graphs, table, clock, timer
  content.py    pictures + themes    ◀─ "req pic ..." / "req themes"   album / mixed view
                                     ── RLE RGB565 pixels ────────▶
  menu clicks   "cmd view 1" ...     ─────────────────────────────▶  touch menu changes
                checkmarks           ◀─ "state view=.. theme=.." ───
```

The firmware draws into a RAM canvas and pushes it to the display with DMA, which is much faster than drawing pixel by
pixel. Pictures are resized on the Mac and sent run-length encoded.

## Parts list

| # | Part | Qty | Notes |
|---|---|---|---|
| 1 | [Particle Xenon](https://docs.particle.io/reference/discontinued/hardware/xenon-datasheet/) | 1 | Feather-format nRF52840 board with male headers (discontinued; check eBay or old stock) |
| 2 | [Adafruit 3.5" 480x320 TFT FeatherWing](https://www.adafruit.com/product/3651) | 1 | HX8357D display, **STMPE610** resistive touch (the original V1 wing), microSD slot (not used) |
| 3 | Micro-USB cable | 1 | Must carry **data**, not just power: it is the link to the Mac |
| 4 | 3D-printed case (optional) | 1 | [Adafruit TFT 3.5" FeatherWing Housing / Enclosure](https://www.thingiverse.com/thing:2836944) by Empor on Thingiverse |
| 5 | A Mac | 1 | Runs the µMonitor app; built and tested on Apple Silicon, macOS 26 |

## Assembly

Plug the Xenon into the female headers on the back of the FeatherWing. The 16- and 12-pin headers only fit one way.
Then put the pair in the case. No soldering or wiring beyond the headers.

Pin mapping (Feather pin → Xenon pin), handled in the firmware:

| Signal | Feather pin | Xenon pin |
|---|---|---|
| TFT CS | 9 | **D4** |
| TFT DC | 10 | **D5** |
| Touch CS (STMPE610) | 6 | **D3** |
| SD CS (kept high, unused) | 5 | D2 |
| SPI | SCK / MOSI / MISO | SCK / MOSI / MISO |

No SD card is needed: pictures and themes stream from the Mac.

## Install

### 1. Prerequisites (Mac)

```bash
brew install python@3.11 dfu-util node
npm install -g particle-cli          # Particle CLI (cloud compiling and flashing)
particle login                       # free Particle account; needed for the cloud compiler
```

### 2. Get the code and the Python environment

```bash
git clone https://github.com/jeaimehp/micromonitor.git
cd micromonitor
python3 -m venv .venv
.venv/bin/pip install psutil pyserial rumps py2app pillow pillow-heif
```

### 3. Put Device OS 1.5.2 on the Xenon (once)

Connect the Xenon by USB and put it in **DFU mode**: hold **MODE**, tap **RESET**, keep holding MODE until the LED
blinks **yellow**, then release.

```bash
particle update --target 1.5.2      # 1.5.2 is the last Device OS that supports the Xenon
```

### 4. Compile and flash the firmware

```bash
firmware/flash.sh firmware/dashboard
```

This compiles in the Particle cloud for `xenon` / Device OS 1.5.2 (libraries come from
`firmware/dashboard/project.properties`) and flashes over USB. The display shows **WAITING FOR HOST** until µMonitor
runs. `flash.sh` finds `particle` on your PATH (or in npm's global bin); override it with `PARTICLE=/path/to/particle`.

### 5. Build and install the µMonitor menu bar app

```bash
.venv/bin/python setup.py py2app      # -> dist/µMonitor.app (self-contained, about 75 MB)
cp -R "dist/µMonitor.app" /Applications/
open "/Applications/µMonitor.app"
```

A small monitor icon shows up in the menu bar (there's no Dock icon), and the display switches to **LIVE**.
Use **Start at Login** in its menu to launch it automatically. Turn that on *after* copying the app to /Applications,
because the login item remembers where the app was.

> **Rebuilding:** the app bundles `host/*.py`, so after changing anything in `host/` rebuild it with
> `rm -rf build dist && .venv/bin/python setup.py py2app`.

### Without the app

`./run.sh -v` streams from the terminal instead (no pictures menu, but albums still work). Run either µMonitor or
`run.sh`, not both: they share the USB port, and the second one waits with "port busy".

## Using it

### On the display

- **Tap** anywhere to open the menu. It closes after 10 seconds or with CLOSE.
  - Choose the view (Dashboard / Album / Mixed), theme, layout, album folder, slideshow speed and rotation
    (Normal, Flipped, Portrait, Portrait flip).
  - Under **More…**: the clock badge on pictures, which side the mixed-view photo goes on, the photo in the
    LCARS/XP/System 7 layouts, and **Calibrate touch** (press the 4 targets).
- **Album**: tap the left or right third for the previous or next picture; the middle opens the menu.
- **Mixed view** and the LCARS/XP/System 7 photo: tap the photo for the next picture.
- **Focus layout**: tap a small tile to make it the big graph.
- **TIME'S UP**: tap the timer to go back to the clock.

### In the µMonitor menu bar app

| Main menu | Timer |
|---|---|
| ![µMonitor main menu]({{ '/images/menubar_main.png' | relative_url }}) | ![Timer submenu]({{ '/images/menubar_timer.png' | relative_url }}) |
| **Display** (mirrors the touch menu) | **Pictures** |
| ![Display submenu]({{ '/images/menubar_display.png' | relative_url }}) | ![Pictures submenu]({{ '/images/menubar_pictures.png' | relative_url }}) |

- **Display**: the same settings as the touch menu, plus Next/Previous Picture and Calibrate Touch…
- **Pictures**: Add Pictures… (copied into `~/Pictures/µMonitor`), Choose Photos Folder…, Open Photos Folder, and
  **Hold Current Photo** (pauses the slideshow; Next Picture still works). JPG, PNG, HEIC and more are supported.
- **Timer**: 1/5/10/15/25/60 minutes, Custom…, Pause/Resume, Cancel. **Stopwatch**: Start/Stop, Reset.
  When a timer finishes, the display flashes and the Mac shows a notification.
- **Streaming** on/off, CPU % in the menu bar, Start at Login, Quit.

Hidden files (anything starting with `.`) and subfolders in the photos folder are ignored. A file that can't be
decoded is skipped.

## Layouts and themes

**Layouts:** Quad, Stacked (8-minute history), Focus, Tiles, LCARS, Tron, XP and System 7. Every view also has a
portrait version.

**Themes:** Dark, Light, Retro Green, Amber, Ocean, Synthwave, Solarized, LCARS, Tron, Windows XP, System 7 and
High Contrast (colorblind-safe graph colors). The [Themes page]({{ '/themes/' | relative_url }}) has screenshots of every one.

<div class="gallery">
{% for t in site.data.themes.layout %}<figure><img src="{{ '/images/' | append: t.image | relative_url }}" alt="{{ t.name }} theme" width="480" height="320" loading="lazy"><figcaption>{{ t.name }}</figcaption></figure>
{% endfor %}</div>

## Customizing

- **Themes**: see the [Themes page]({{ '/themes/' | relative_url }}) for a full walkthrough. In short, edit `THEMES` in `tools/make_themes.py`, then run `.venv/bin/python tools/make_themes.py`. It writes
  `content/themes/*.thm` (streamed by the app) and `firmware/dashboard/src/builtin_themes.h` (Dark and Light, built
  into the firmware), and checks text contrast. Rebuild the app, and reflash if you changed a built-in theme.
- **Pixel art**: sprites and captions live in `tools/make_pixelart.py`. Run it to regenerate `content/motivation/`.
- **Screenshots**: with µMonitor quit, `.venv/bin/python tools/uitest.py "wait 5, shot NAME" --snapdir DIR` reads the
  display's frame memory back over USB and saves `DIR/NAME.png`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Display says **NO HOST DATA** | Start µMonitor (or `./run.sh`); check that **Streaming** is on |
| Menu says **Xenon port busy** | Another sender (`run.sh`, a serial monitor) has the port; quit it |
| Taps land in the wrong place | Display menu **More… → Calibrate**, or µMonitor **Display → Calibrate Touch…** |
| Album says **NO PHOTOS YET** | µMonitor **Pictures → Add Pictures…** |
| Some processes are missing | macOS hides root-owned processes (e.g. WindowServer) unless the collector runs as root |
| `particle update` can't find the device | Put the Xenon in DFU mode (blinking yellow) and retry |

Still stuck? See the [FAQ]({{ '/faq/' | relative_url }}) or [get in touch]({{ '/contact/' | relative_url }}).

## Project layout

```
firmware/dashboard/   Device firmware (C++, Device OS 1.5.2)
firmware/flash.sh     Cloud compile + USB flash helper
host/                 µMonitor: menubar.py (app), sender.py (USB streamer), collector.py (metrics),
                      content.py (pictures/themes), timers.py (timer/stopwatch)
content/              Themes and motivation pixel art (bundled into the app)
tools/                Theme/pixel-art/icon generators, uitest.py, install-launchd.sh
docs/                 This website (Jekyll, served by GitHub Pages)
setup.py              py2app build for µMonitor
```
