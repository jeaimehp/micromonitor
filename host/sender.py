#!/usr/bin/env python3
"""Stream collector samples to the Xenon over USB serial every 2s, reconnecting as needed.

Usage: sender.py [--port /dev/cu.usbmodemXXXX] [--count N] [--verbose]
"""
import argparse
import glob
import sys
import threading
import time

import serial

from collector import INTERVAL, Collector, encode


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


class Streamer:
    """Samples every INTERVAL and writes each sample to the device. Safe to run in a thread; call stop() to end."""

    def __init__(self, port=None, verbose=False, on_status=None):
        self.port_arg = port
        self.verbose = verbose
        self.on_status = on_status or (lambda streamer: None)
        self._stop = threading.Event()
        self.ser = None
        self.connected_port = None
        self.open_error = None  # last failure to open an existing port (e.g. held by another sender)
        self.sent = 0
        self.last_ack = ""
        self.last_sample = None

    def stop(self):
        self._stop.set()

    @property
    def stopped(self):
        return self._stop.is_set()

    def _open(self):
        port = self.port_arg or find_port()
        if not port:
            self.open_error = None
            return
        try:
            self.ser = serial.Serial(port, 115200, timeout=0, write_timeout=1, exclusive=True)
            self.connected_port = port
            self.open_error = None
            log(f"connected {port}")
        except serial.SerialException as e:
            self.open_error = str(e)
            log(f"open {port} failed: {e}")

    def _close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.connected_port = None

    def _drain(self):
        """Read whatever the device has sent back (acks/errors)."""
        data = self.ser.read(4096)
        for line in data.decode(errors="replace").splitlines():
            line = line.strip()
            if line:
                self.last_ack = line
                if self.verbose:
                    log(f"< {line}")

    def run(self, count=0):
        collector = Collector()
        next_t = time.monotonic() + INTERVAL
        while not self.stopped and (not count or self.sent < count):
            if self._stop.wait(max(0.0, next_t - time.monotonic())):
                break
            next_t += INTERVAL
            self.last_sample = collector.sample()
            line = encode(self.last_sample)
            if self.ser is None:
                self._open()
                if self.ser is None:
                    log("device not found, retrying")
                    self.on_status(self)
                    continue
            try:
                self._drain()
                self.ser.write(line.encode() + b"\n")
                self.sent += 1
                if self.verbose:
                    log(f"> {line}")
            except (serial.SerialException, OSError) as e:
                log(f"serial error: {e}; reconnecting")
                self._close()
            self.on_status(self)
        if self.ser:
            time.sleep(0.5)
            try:
                self._drain()
            except (serial.SerialException, OSError):
                pass
        self._close()
        self.on_status(self)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--count", type=int, default=0, help="stop after N samples (0 = forever)")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()
    Streamer(args.port, args.verbose).run(args.count)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
