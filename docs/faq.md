---
layout: default
title: FAQ
permalink: /faq/
description: Frequently asked questions about µMonitor.
---

# FAQ

<details markdown="1">
<summary>What does µMonitor show?</summary>

Live CPU and GPU utilization, RAM, disk read/write and usage, network rx/tx, the top 5 processes, the date and time,
a photo slideshow, and a timer/stopwatch. Data refreshes every 2 seconds.
</details>

<details markdown="1">
<summary>Does it work on Windows or Linux?</summary>

Not yet. The menu bar app uses macOS-only pieces (`rumps`, `py2app`, and `ioreg` for the GPU). The streamer
(`host/sender.py` and `collector.py`) is plain Python with `psutil` and `pyserial`, so a Linux or Windows port of
the terminal mode (`./run.sh`) is mostly a matter of replacing the GPU reading and the port detection.
</details>

<details markdown="1">
<summary>Does it need Wi-Fi or the internet?</summary>

No. Everything goes over the USB cable. You only need the internet once, to compile the firmware in the Particle
cloud and to install the Python packages.
</details>

<details markdown="1">
<summary>Where can I get a Particle Xenon? It's discontinued.</summary>

Check eBay or other second-hand and old-stock sellers. If you can't find one, see the
[ESP32 notes]({{ '/esp32/' | relative_url }}) for how the project might be rebuilt on an ESP32 Feather (untested).
</details>

<details markdown="1">
<summary>Can I use an ESP32 instead?</summary>

Probably, but it hasn't been tested. The [ESP32 page]({{ '/esp32/' | relative_url }}) lists the code that would
need to change, which boards look like the best fit, and the pin and serial-port differences.
</details>

<details markdown="1">
<summary>My FeatherWing's touch doesn't work at all.</summary>

The firmware expects the original (V1) 3.5" TFT FeatherWing with the **STMPE610** touch controller. The newer V2
wing uses a **TSC2007** I²C touch controller, which needs a different library in `touch.cpp`. If touch works but
taps land in the wrong place, run **Calibrate** from the display menu (More…) or the µMonitor Display menu.
</details>

<details markdown="1">
<summary>Do I need an SD card?</summary>

No. Pictures and themes stream from the Mac over USB. The wing's SD slot wasn't reliable in SPI mode with this
setup, so it's not used.
</details>

<details markdown="1">
<summary>The display says NO HOST DATA or WAITING FOR HOST.</summary>

Start the µMonitor app (or `./run.sh`) and check that **Streaming** is on in its menu. Make sure the USB cable carries
data, not just power. Only one program can use the port at a time; quit any serial monitor or second copy of
`run.sh`.
</details>

<details markdown="1">
<summary>Why are some processes missing from the top 5?</summary>

macOS hides details of root-owned processes (such as WindowServer) from normal users. They show up only if the
collector runs as root.
</details>

<details markdown="1">
<summary>Which photo formats work?</summary>

JPG, PNG, HEIC and anything else Pillow can open. Photos are resized on the Mac before they're sent, and take 1–3
seconds to load. Hidden files and subfolders are ignored.
</details>

<details markdown="1">
<summary>Do my settings survive unplugging?</summary>

Yes. The view, theme, layout, rotation and touch calibration are stored in the Xenon's EEPROM.
</details>

<details markdown="1">
<summary>Can I make my own theme?</summary>

Yes. Edit `THEMES` in `tools/make_themes.py` and run it. It writes the theme files the app streams to the display and
checks the text contrast. The [Themes page]({{ '/themes/' | relative_url }}) walks through it step by step, and
community themes are welcome.
</details>

<details markdown="1">
<summary>Is there a case?</summary>

The project uses Empor's [3.5" FeatherWing enclosure](https://www.thingiverse.com/thing:2836944) on Thingiverse,
printed in PLA. It's optional.
</details>

<details markdown="1">
<summary>What's the license?</summary>

MIT. The firmware uses the Adafruit HX8357, GFX and STMPE610 libraries (Particle ports by rickkas7), which carry
their own BSD licenses.
</details>

Didn't find your answer? [Get in touch]({{ '/contact/' | relative_url }}).
