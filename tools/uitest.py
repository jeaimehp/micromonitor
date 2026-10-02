#!/usr/bin/env python3
"""Drive the device UI over serial while streaming real samples.

Usage: uitest.py <script> [--snapdir DIR]
script: comma-separated steps: "tap X Y", "send TEXT" (any line, e.g. "send pictest 1 0 480 320"),
"timer SECONDS", "stopwatch" (start/stop), "pause" (timer pause/resume), "cancel" (timer/stopwatch off),
"wait SECONDS", "snap NAME" (webcam photo to SNAPDIR/NAME.jpg), "snapfull NAME" (full resolution).
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


def reader():
    buf = b""
    while not done.is_set():
        buf += ser.read(512)
        while b"\n" in buf:
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
    elif kind == "snapfull":  # full 1920x1080 webcam frame (for README photos)
        out = os.path.join(args.snapdir, rest[0] + ".jpg")
        subprocess.run(["imagesnap", "-d", "HD Pro Webcam C920", "-w", "2", out], capture_output=True)
done.set()
time.sleep(0.3)
