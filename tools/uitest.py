#!/usr/bin/env python3
"""Drive the device UI over serial while streaming real samples.

Usage: uitest.py <script> [--snapdir DIR]
script: comma-separated steps: "tap X Y", "wait SECONDS", "snap NAME" (webcam photo to SNAPDIR/NAME.jpg).
Prints every line the device sends (acks, taps, menu events).
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
from collector import INTERVAL, Collector, encode  # noqa: E402

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


def reader():
    buf = b""
    while not done.is_set():
        buf += ser.read(512)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.decode(errors="replace").strip()
            if line:
                print(f"{time.time() - t0:6.1f} < {line}", flush=True)


def streamer():
    c = Collector()
    while not done.wait(INTERVAL):
        send(encode(c.sample()))


threading.Thread(target=reader, daemon=True).start()
threading.Thread(target=streamer, daemon=True).start()
for step in [s.strip() for s in args.script.split(",") if s.strip()]:
    kind, *rest = step.split()
    print(f"{time.time() - t0:6.1f} > {step}", flush=True)
    if kind == "tap":
        send(step)
    elif kind == "wait":
        time.sleep(float(rest[0]))
    elif kind == "snap":
        out = os.path.join(args.snapdir, rest[0] + ".jpg")
        subprocess.run([os.path.join(os.path.dirname(__file__), "snap.sh"), out], capture_output=True)
done.set()
time.sleep(0.3)
