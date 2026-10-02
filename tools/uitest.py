#!/usr/bin/env python3
"""Drive the device UI over serial while streaming real samples.

Usage: uitest.py <script> [--snapdir DIR]
script: comma-separated steps: "tap X Y", "send TEXT" (any line, e.g. "send pictest 1 0 480 320"),
"timer SECONDS", "stopwatch" (start/stop), "pause" (timer pause/resume), "cancel" (timer/stopwatch off),
"wait SECONDS", "snap NAME" (webcam photo to SNAPDIR/NAME.jpg), "snapfull NAME" (full resolution),
"shot NAME" (pixel-exact screenshot read from the display's memory -> SNAPDIR/NAME.png).
Prints every line the device sends (acks, taps, menu events) and serves its theme/picture requests like µMonitor.
"""
import argparse
import glob
import os
import subprocess
import sys
import threading
import time

import serial

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "host"))
import content  # noqa: E402
from collector import INTERVAL, Collector, encode  # noqa: E402
from timers import TimerState  # noqa: E402

timers = TimerState()

ap = argparse.ArgumentParser()
ap.add_argument("script")
ap.add_argument("--snapdir", default=".")
args = ap.parse_args()

ser = serial.Serial(sorted(glob.glob("/dev/cu.usbmodem*"))[0], 115200, timeout=0.1, exclusive=True)
ser.write(b"\n")  # terminate any half line the device kept from a previous connection
ser.write(b"cmd themes\n")  # make the device fetch this checkout's themes
lock = threading.Lock()
done = threading.Event()
t0 = time.time()


def send(line):
    with lock:
        ser.write((line + "\n").encode())


def serve(parts):
    with lock:
        ser.write_timeout = 15
        if parts[1:2] == ["themes"]:
            ser.write(("\n".join(content.theme_lines()) + "\n").encode())
        elif parts[1:2] == ["pic"]:
            folder, n, w, h = (int(x) for x in parts[2:6])
            count, payload = content.render_picture(folder, n, w, h)
            ser.write(f"pic {count} {w if count else 0} {h if count else 0} rle\n".encode() + payload)


pending_shot = []  # names waiting for a "shot W H" reply


def save_shot(name, w, h, data):
    from PIL import Image
    img = Image.frombytes("RGB", (w, h), data)
    # The display returns 6 significant bits per channel; spread them over the full 8-bit range.
    img = img.point(lambda v: (v & 0xFC) | (v >> 6))
    img.save(os.path.join(args.snapdir, name + ".png"))
    print(f"{time.time() - t0:6.1f} = saved screenshot {name}.png {w}x{h}", flush=True)


def reader():
    buf = b""
    while not done.is_set():
        buf += ser.read(4096)
        while b"\n" in buf:
            if buf.startswith(b"shot ") and b"\n" in buf:
                header, rest = buf.split(b"\n", 1)
                w, h = (int(x) for x in header.split()[1:3])
                need = w * h * 3
                while len(rest) < need:
                    rest += ser.read(need - len(rest))
                save_shot(pending_shot.pop(0) if pending_shot else "shot", w, h, rest[:need])
                buf = rest[need:]
                continue
            line, buf = buf.split(b"\n", 1)
            line = line.decode(errors="replace").strip()
            if line:
                print(f"{time.time() - t0:6.1f} < {line}", flush=True)
                if line.startswith("req "):
                    serve(line.split())
                elif line == "evt timer_dismiss":
                    timers.cancel()


def streamer():
    c = Collector()
    while not done.wait(INTERVAL):
        s = c.sample()
        tm = timers.sample()
        if tm:
            s["tm"] = tm
        send(encode(s))


threading.Thread(target=reader, daemon=True).start()
threading.Thread(target=streamer, daemon=True).start()
for step in [s.strip() for s in args.script.split(",") if s.strip()]:
    kind, *rest = step.split()
    print(f"{time.time() - t0:6.1f} > {step}", flush=True)
    if kind == "tap":
        send(step)
    elif kind == "send":
        send(step[5:])
    elif kind == "timer":
        timers.start_timer(float(rest[0]))
    elif kind == "stopwatch":
        timers.stopwatch_start_stop()
    elif kind == "pause":
        timers.toggle_pause()
    elif kind == "cancel":
        timers.cancel()
    elif kind == "wait":
        time.sleep(float(rest[0]))
    elif kind == "snap":
        out = os.path.join(args.snapdir, rest[0] + ".jpg")
        subprocess.run([os.path.join(os.path.dirname(__file__), "snap.sh"), out], capture_output=True)
    elif kind == "shot":  # screenshot read back from the display's memory
        pending_shot.append(rest[0])
        send("shot")
    elif kind == "snapfull":  # full 1920x1080 webcam frame (for README photos)
        out = os.path.join(args.snapdir, rest[0] + ".jpg")
        subprocess.run(["imagesnap", "-d", "HD Pro Webcam C920", "-w", "2", out], capture_output=True)
done.set()
time.sleep(0.3)
