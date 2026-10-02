#!/usr/bin/env python3
"""Send synthetic samples to the device to check graph geometry.

Usage: synthetic.py 'r=25*10,r=50*10' [base key=value ...]
Each segment is key=value*count; the other keys hold the base values.
"""
import glob
import json
import sys
import time

import serial

base = {"c": 0, "r": 0, "ru": 0, "rt": 64, "d": 0, "dr": 0, "dw": 0, "nr": 0, "nt": 0,
        "p": [["proc%d" % i, 0.0, 0.0] for i in range(5)]}
for kv in sys.argv[2:]:
    k, v = kv.split("=")
    base[k] = float(v)
ser = serial.Serial(sorted(glob.glob("/dev/cu.usbmodem*"))[0], 115200, timeout=1)
for seg in sys.argv[1].split(","):
    kv, n = seg.split("*")
    k, v = kv.split("=")
    for _ in range(int(n)):
        sample = dict(base, **{k: float(v)})
        ser.write((json.dumps(sample, separators=(",", ":")) + "\n").encode())
        ser.readline()
        time.sleep(0.2)
