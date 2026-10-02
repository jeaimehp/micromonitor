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

import content
from collector import INTERVAL, Collector, encode
from timers import TimerState


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
        self._rx = b""
        self._wlock = threading.Lock()   # the app's menu thread sends commands while this thread streams
        self.timers = TimerState()
        self.device_state = {}           # parsed from the device's "state ..." lines
        self.device_themes = []

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
        with self._wlock:
            if self.ser:
                try:
                    self.ser.close()
                except Exception:
                    pass
            self.ser = None
            self.connected_port = None

    def _drain(self):
        """Read whatever the device has sent back: acks/errors, and requests for themes or pictures."""
        self._rx += self.ser.read(4096)
        while b"\n" in self._rx:
            raw, self._rx = self._rx.split(b"\n", 1)
            line = raw.decode(errors="replace").strip()
            if not line:
                continue
            if self.verbose:
                log(f"< {line}")
            if line.startswith("req "):
                self._serve(line.split())
            elif line.startswith("state "):
                self._parse_state(line)
            elif line == "evt timer_dismiss":
                self.timers.cancel()  # tapped TIME'S UP on the display
            else:
                self.last_ack = line

    def _parse_state(self, line):
        state = {}
        for part in line[6:].split(" themes=", 1)[0].split():
            k, _, v = part.partition("=")
            if v.lstrip("-").isdigit():
                state[k] = int(v)
        self.device_state = state
        if " themes=" in line:
            self.device_themes = line.split(" themes=", 1)[1].split(",")

    def command(self, text):
        """Send a "cmd ..." line to the device (from any thread). Returns False when not connected."""
        with self._wlock:
            if self.ser is None:
                return False
            try:
                self.ser.write(f"cmd {text}\n".encode())
                return True
            except (serial.SerialException, OSError):
                return False

    def _send_blocking(self, data):
        # Pictures are large; the device reads them as fast as it can draw, so allow a long write.
        with self._wlock:
            self.ser.write_timeout = 15
            try:
                self.ser.write(data)
            finally:
                self.ser.write_timeout = 1

    def _serve(self, parts):
        if parts[1:2] == ["themes"]:
            self._send_blocking(("\n".join(content.theme_lines()) + "\n").encode())
        elif parts[1:2] == ["pic"] and len(parts) == 6:
            folder, n, w, h = (int(x) for x in parts[2:6])
            t0 = time.monotonic()
            count, payload = content.render_picture(folder, n, w, h)
            self._send_blocking(f"pic {count} {w if count else 0} {h if count else 0} rle\n".encode() + payload)
            if self.verbose:
                log(f"served pic folder={folder} n={n} {w}x{h} count={count} in {time.monotonic() - t0:.2f}s")

    def run(self, count=0):
        collector = Collector()
        next_t = time.monotonic() + INTERVAL
        while not self.stopped and (not count or self.sent < count):
            if self._stop.wait(max(0.0, next_t - time.monotonic())):
                break
            next_t += INTERVAL
            self.last_sample = collector.sample()
            tm = self.timers.sample()
            if tm:
                self.last_sample["tm"] = tm
            line = encode(self.last_sample)
            if self.ser is None:
                self._open()
                if self.ser is not None:
                    self.command("state")  # so the app menu can show the device's current settings
                if self.ser is None:
                    log("device not found, retrying")
                    self.on_status(self)
                    continue
            try:
                self._drain()
                with self._wlock:
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
