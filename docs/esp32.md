---
layout: default
title: Building with an ESP32
permalink: /esp32/
description: Notes on porting µMonitor from the discontinued Particle Xenon to an ESP32 Feather. Untested.
---

# Building with an ESP32

> **Untested.** µMonitor has only been built and run on a Particle Xenon. This page lists ways the project could
> be recreated on an ESP32, based on how the firmware is written, but none of it has been tried on real hardware.
> Expect to debug. If you get it working, please [let me know]({{ '/contact/' | relative_url }}).
{: .callout-warn}

The Particle Xenon is discontinued, and the last Device OS that supports it (1.5.2) is frozen. An ESP32 Feather is
the obvious replacement: it's cheap, easy to find, uses the same Feather footprint, and plugs straight into the
3.5" TFT FeatherWing.

## Which board

| Board | Why it might work | Things to watch |
|---|---|---|
| **Adafruit ESP32-S3 Feather** (with PSRAM) | **Recommended.** Native USB, so the Mac sees a fast `/dev/cu.usbmodem*` port like the Xenon's. 512 KB SRAM plus PSRAM, enough for a full 480×320 frame buffer. | Choose the PSRAM version if you want a full-screen canvas |
| Adafruit ESP32-S2 Feather | Native USB, single core | Less RAM than the S3; keep the strip-sized canvas |
| Adafruit HUZZAH32 (original ESP32 Feather) | Common and cheap | USB goes through a USB-to-serial chip, so the link is limited by the baud rate (see [Serial link](#serial-link)) |

Any ESP32 board with SPI will do if you wire it yourself, but a Feather plugs into the FeatherWing with no wiring.

## Display and touch

- **FeatherWing version:** the firmware drives the **V1** wing with the **STMPE610** touch controller. Newer
  FeatherWings (V2) use a **TSC2007** I²C touch controller instead. On a V2 wing, swap `Adafruit_STMPE610` for the
  `Adafruit_TSC2007` library in `touch.cpp`. The calibration code should otherwise carry over.
- **Pins:** the wing's chip-select pins land on different GPIOs on each Feather. Check Adafruit's
  [FeatherWing pinouts guide](https://learn.adafruit.com/adafruit-3-5-tft-featherwing/pinouts) for your board and
  update the constants in `firmware/dashboard/src/app.h`:

  ```cpp
  // Xenon (current)              // ESP32 Feather: look these up for your board
  const int TFT_CS = D4;          const int TFT_CS = /* Feather pin 9's GPIO  */;
  const int TFT_DC = D5;          const int TFT_DC = /* Feather pin 10's GPIO */;
  const int SD_CS  = D2;          const int SD_CS  = /* Feather pin 5's GPIO  */;
  const int TS_CS  = D3;          const int TS_CS  = /* Feather pin 6's GPIO  */;
  ```

  Adafruit's examples use 15 / 33 / 14 / 32 (TFT_CS / TFT_DC / SD_CS / STMPE_CS) on the original HUZZAH32.
  On the ESP32-S2 and S3 Feathers the GPIO numbers match the silkscreen labels (9 / 10 / 5 / 6).

## Porting the firmware

The firmware is Arduino-style C++, so most of it (layouts, themes, graphs, menu, album decoding, JSON parsing)
should compile unchanged. The Particle-specific parts are small and in a few places:

| Particle (Xenon) | ESP32 (Arduino core) replacement |
|---|---|
| `#include "Particle.h"` | `#include <Arduino.h>`, `<SPI.h>`, `<EEPROM.h>` |
| `SYSTEM_MODE(MANUAL)`, `SYSTEM_THREAD(ENABLED)` (`main.cpp`) | Remove; not needed |
| `Adafruit_HX8357_RK` (Particle library) | Adafruit **HX8357**, **GFX** and **STMPE610** libraries from the Arduino Library Manager / PlatformIO |
| `SPI.transfer(buf, NULL, n, NULL)` DMA push (`gfx.cpp`) | `SPI.writeBytes(buf, n)`, or move the display code to a library with ESP32 DMA (e.g. TFT_eSPI or LovyanGFX) |
| `SPI.transfer(tx, rx, n, NULL)` frame read-back for screenshots | `SPI.transferBytes(tx, rx, n)` |
| `EEPROM.get` / `EEPROM.put` (`settings.cpp`) | Call `EEPROM.begin(size)` in `setup()` and `EEPROM.commit()` after each `put`, or switch to `Preferences` |
| `ApplicationWatchdog(10000, System.reset, …)` | `esp_task_wdt_init` / `esp_task_wdt_add`, and `ESP.restart()` |
| `System.freeMemory()` | `ESP.getFreeHeap()` |
| `firmware/flash.sh` (Particle cloud compile) | Build and flash with the Arduino IDE or PlatformIO (`pio run -t upload`) |

**Canvas size:** the Xenon has 256 KB of RAM, so the firmware draws in strips (`CANVAS_PIXELS = 240 * 110` in
`app.h`). An ESP32 can keep the same strip approach. An ESP32-S3 with PSRAM could hold the whole 480×320 frame
(about 300 KB) and redraw in one pass, but PSRAM is slower to DMA from, so keep the strip buffer in internal RAM.

## Serial link

The Mac side (`host/sender.py`) looks for `/dev/cu.usbmodem*` and opens it at 115200 baud.

- **ESP32-S2 / S3 (native USB CDC):** the port shows up as `/dev/cu.usbmodem*` and the baud rate is ignored, as on
  the Xenon, so the app should find it with no changes. In the Arduino IDE, enable **USB CDC On Boot**.
- **Original ESP32 (USB-to-serial chip):** the port is named `/dev/cu.usbserial-*`, `/dev/cu.SLAB_USBtoUART` or
  `/dev/cu.wchusbserial*`. Start the streamer with `./run.sh --port /dev/cu.usbserial-XXXX`, or extend `find_port()`
  in `host/sender.py`. 115200 baud really is 115200 here (about 11 KB/s), which is fine for the metrics but makes
  photos slow. Raise it to 921600 or more on both sides (`Serial.begin` and `serial.Serial(...)`).
  Opening the port can also reset the board through the DTR/RTS lines; if it reboots every time the app connects,
  set `ser.dtr = False` and `ser.rts = False` before opening.

## Bonus ideas (also untested)

- **Wi-Fi instead of USB:** the protocol is newline-delimited JSON plus RLE pixel data, so it could run over a TCP
  socket. The display could then sit anywhere in the house, powered by any USB charger.
- **Bluetooth LE:** the ESP32-S3 has BLE, but the throughput is low, so it's better suited to metrics than photos.

If you try any of this, I'd love to hear how it went: [contact]({{ '/contact/' | relative_url }}).
