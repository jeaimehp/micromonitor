---
layout: default
title: The making of µMonitor
permalink: /making-of/
description: How µMonitor was designed and built, step by step, from the first prompt to the nostalgia themes.
---

# The making of µMonitor
{:.no_toc}

µMonitor was built in a single long afternoon and evening, October 2, 2026, by Je'aime Powell working with Claude Code,
an AI coding agent. Je'aime set the direction one prompt at a time, and the agent wrote the code, flashed the board and
checked every step through a webcam pointed at the display. This page tells that story from the project's handoff log,
[`pickupandgo.md`]({{ site.repo }}/blob/main/pickupandgo.md), with the **original prompts** quoted as they were typed.

* TOC
{:toc}

## The first prompt

It started with a board from the drawer: a **Particle Xenon**, a capable nRF52840 board that Particle had discontinued,
plugged into an **Adafruit 3.5" TFT FeatherWing**. This is the prompt that began the project:

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 1:40 PM · the founding prompt</span>

I have a particle xenon microcontroller with a adafruit feather 3.5" 480x320 tft featherwing display connected by usb
in dfu mode. I also have a 16gb sdcard plugged into this mac that can be inserted into the sdcard slot on the display.
Create a dashboard that displays the current status of this computers network, cpu, ram, and harddrive resources in
graph form that updates every 2 seconds on the screen. Also include a list of the top 5 processess running and their
resource usage. Implement each feature one at a time testing the implementations as you go. If the sdcard is need as
me to insert it and wait for me to do it. update a file named pickupandgo.md such that another agent has the
information to pickup from any given step. Also use git to track revision history. before starting generate a plan for
the implementation and wait for approval.
</div>

That prompt set the working style for everything after it: a plan approved up front, then numbered steps that each
end the same way, **test, commit, update the log**. The handoff file meant that anyone, human or agent, could pick the
work up mid-stream.

The first design decision shaped the rest: **the Mac does the thinking, the Xenon only draws.** A Python collector built
on `psutil` gathers the metrics and sends one line of JSON over USB every two seconds; the board parses it and paints
the screen. Device OS 1.5.2, the last release that supports the Xenon, went on first, then a "hello" heartbeat
firmware, then the pin map from the FeatherWing onto the Xenon.

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 2:38 PM</span>

logged in, logitech camera pointing at screen
</div>

From then on the webcam was the project's eyes: a test pattern on the panel, photographed and checked, then every graph
after it.

## From 3 seconds to 185 milliseconds

The first real problem came at the serial-link step. Drawing with the standard graphics calls sent one SPI transaction
per pixel or character, and a few lines of text took **about 3 seconds**. The fix became the project's core technique:
draw everything into a 52 KB canvas in RAM, then push it to the display in a single DMA transfer. A full redraw went
from **2,945 ms to 185 ms**, and every feature since is built on that canvas-and-strip renderer.

## The dashboard, one graph at a time

Each panel was added on its own and checked against real load, not just synthetic numbers:

- **CPU**: a burst of eight `yes` processes showed a plateau near 50% across 18 cores.
- **RAM**: stepped through 25/50/75/100% with synthetic samples to check the scaling.
- **Disk**: a real 4 GB uncached write and read showed spikes of about 2.1 GB/s.
- **Network**: a 1 GB download (about 38 MB/s) and an upload (about 23 MB/s).
- **Top processes**: two `yes` processes appeared at the top at 100.0, matching the JSON exactly.

Polish followed: a stale-data indicator, automatic reconnects, and a 12-minute soak test.

![The Quad dashboard]({{ '/images/dashboard_quad.jpg' | relative_url }})

## A menu bar app named with a Greek letter

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 3:42 PM</span>

is anything running locally on the mac for the software to work?
</div>

The answer was a Python script in a terminal, which led straight to the next request:

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 3:44 PM</span>

turn run into a menu bar macos app that runs as a status icon
</div>

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 3:48 PM</span>

rename the app micromonitor with the micro symbol for the word micro
</div>

And so it became **µMonitor**: a menu bar app built with `rumps`, packaged with `py2app`, with a tiny monitor icon drawn
pixel by pixel in Python.

## Touch, themes and layouts

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 3:54 PM</span>

the screen is a touchscreen, add a menu that can be clicked. Include options for rotating the screen, themes, and a
mixed view that allows pictures saved on the sdcard to display as a photo albumn both with and without the system
resources. create a sample set of funny cute motivation pixel art/cartoon pictures that can display from a diffeent
folder on the sdcard, also add a set of example themes and save them on the sdcard.
</div>

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 3:56 PM</span>

also add layout options with the themes
</div>

That became Phase 2, with its own approved spec. The touch controller turned out to be an **STMPE610** (the original
V1 FeatherWing), and a guided four-corner calibration was measured and saved to EEPROM.

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 4:22 PM</span>

touch looks good
</div>

Then came the touch menu, settings that survive power-off, and themes generated by a script that rejects colors with
too little contrast and checks graph colors for colorblind viewers. There were four layouts (Quad, Stacked, Focus,
Tiles), a clock synced from the Mac, a cleaner FreeSans font, rounded corners, and a GPU reading from `ioreg`.

![The touch menu]({{ '/images/menu.jpg' | relative_url }})

The pixel-art "motivation" cards (a cat, a cactus, a sloth, a coffee cup, a robot and more) were drawn as ASCII art in
Python. One of them didn't make the cut:

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 4:47 PM</span>

before that please remove any ghost images from the motivation sheet images
</div>

## The SD card that wouldn't cooperate

The photos and themes were meant to live on the FeatherWing's microSD card. The card was formatted, filled,
checksummed and inserted, but it **mounted only 10–50% of the time**, at every SPI clock speed, with or without touch,
with or without DMA. A dedicated probe firmware pointed to a physical problem with the card in SPI mode.

So the plan changed: **skip the SD card and stream everything from the Mac.** Pictures are resized on the fly and sent
run-length encoded (pixel art is about 36× smaller), and themes travel as short text lines. The SD code was deleted.
The workaround turned out simpler than the original plan: there are no files to copy, and photos can be added straight
from the menu bar.

## Features from the requests

From there, the log reads like a list of requests made real: the Album and Mixed views, the Mac menu mirroring the
touch menu in both directions, and a timer and stopwatch.

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 5:42 PM</span>

when the timer is up allow the user on the screen to click it to switch it back to the clock
</div>

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 6:35 PM</span>

portrait mode remains horizontal
</div>

That bug report led to a portrait layout for every view, not just the album.

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 7:07 PM</span>

add a 1 min, 5 min, 30 min, and custom amount to the slide show times
</div>

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 7:47 PM</span>

add a hold current photo to the menubar app
</div>

Along the way, the firmware learned to **read its own screen memory back over USB**, which made pixel-exact screenshots
possible. They're used all over this site.

| Album with clock badge | Mixed view |
|---|---|
| ![Album]({{ '/images/album.jpg' | relative_url }}) | ![Mixed view]({{ '/images/mixed.jpg' | relative_url }}) |

## Going public

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 5:53 PM</span>

create a remote repo on github.com/jeaimehp and push the project to it including a readme that explains how to compile
and install it and photos
</div>

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 6:12 PM</span>

mit license
</div>

Before the first push, the history was rewritten: a sample macOS wallpaper was removed because it can't be
redistributed, and the device's serial number was redacted. The README got a parts list, the board and display specs,
full install steps, and photos.

## The nostalgia themes

The most playful chapter came last: themes that don't just change colors but recreate whole interfaces, each drawn from
scratch in C++ on a 480×320 screen.

- **LCARS**: the Star Trek computer, with elbow frames, a sidebar of readouts and a stardate.
- **Tron**: built from a still of Flynn's terminal in *Tron: Legacy*.

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 7:33 PM · with a still from <i>Tron: Legacy</i></span>

use this image
</div>

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 7:41 PM</span>

that is so cool!
</div>

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 2, 8:14 PM</span>

make a windowsxp theme
</div>

- **Windows XP**: Task Manager's Performance tab on the Bliss hill, with a notification balloon when a timer ends.
- **System 7**: *About This Macintosh*, a CPU Meter, and an alert box with an OK button.

| LCARS | Tron |
|---|---|
| ![LCARS]({{ '/images/lcars_landscape.png' | relative_url }}) | ![Tron]({{ '/images/tron_landscape.png' | relative_url }}) |
| **Windows XP** | **System 7** |
| ![Windows XP]({{ '/images/xp_photo.png' | relative_url }}) | ![System 7]({{ '/images/system7_photo.png' | relative_url }}) |

## Five days later

<div class="prompt" markdown="1">
<span class="prompt-meta">Oct 7, 10:09 AM</span>

The xenon appeared to lock up. I hard reset it by unplugging and then replugging the power but do you know why it
locked? … I think the computer went to sleep so it had an inactive link
</div>

That was it. A sleeping Mac can leave the USB port open but stop reading it, so the board's outgoing buffer filled up
and its writes blocked forever. The fix makes the board drop output nobody is reading instead of waiting, and adds a
10-second watchdog that resets the board if it ever hangs for another reason. "Firmware: don't lock up when the Mac
sleeps" was the last firmware change before this website was built.

## By the numbers

- **About 7 hours** from the first prompt to the System 7 theme
- **21 planned steps** (0–20), each tested, committed and logged
- **8 layouts and 12 themes**, every one available in landscape and portrait
- **185 ms** for a full redraw, down from almost 3 seconds
- **0** SD cards needed in the end

Want to build one? Start with the [documentation]({{ '/documentation/' | relative_url }}), or
[design a theme]({{ '/themes/' | relative_url }}) of your own.
