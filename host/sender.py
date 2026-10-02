#!/usr/bin/env python3
"""Stream collector samples to the Xenon over USB serial every 2s, reconnecting as needed.

Usage: sender.py [--port /dev/cu.usbmodemXXXX] [--count N] [--verbose]
"""
import argparse
import glob
import sys
import time

import serial

from collector import INTERVAL, Collector, encode


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def open_port(port_arg):
    port = port_arg or find_port()
    if not port:
        return None
    try:
        return serial.Serial(port, 115200, timeout=0, write_timeout=1)
    except serial.SerialException as e:
        log(f"open {port} failed: {e}")
        return None


def drain(ser, verbose):
    """Print whatever the device has sent back (acks/errors)."""
    data = ser.read(4096)
    if data and verbose:
        for line in data.decode(errors="replace").splitlines():
            if line.strip():
                log(f"< {line.strip()}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--count", type=int, default=0, help="stop after N samples (0 = forever)")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    collector = Collector()
    ser = None
    sent = 0
    next_t = time.monotonic() + INTERVAL
    while not args.count or sent < args.count:
        time.sleep(max(0.0, next_t - time.monotonic()))
        next_t += INTERVAL
        line = encode(collector.sample())
        if ser is None:
            ser = open_port(args.port)
            if ser is None:
                log("device not found, retrying")
                continue
            log(f"connected {ser.port}")
        try:
            drain(ser, args.verbose)
            ser.write(line.encode() + b"\n")
            sent += 1
            if args.verbose:
                log(f"> {line}")
        except (serial.SerialException, OSError) as e:
            log(f"serial error: {e}; reconnecting")
            try:
                ser.close()
            except Exception:
                pass
            ser = None
    if ser:
        time.sleep(0.5)
        drain(ser, args.verbose)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
