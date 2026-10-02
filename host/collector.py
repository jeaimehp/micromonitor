#!/usr/bin/env python3
"""Collect Mac system metrics with psutil and emit one compact JSON line per sample.

Line format (keys kept short to save serial bandwidth / device RAM):
  {"c":12.5,"r":63.1,"ru":10.1,"rt":16.0,"d":41.2,"dr":0.3,"dw":1.2,
   "nr":12.4,"nt":3.1,"p":[["name",cpu,mem],...5]}
  c=cpu%  r=ram%  ru/rt=ram used/total GB  d=disk used%  dr/dw=disk read/write MB/s
  nr/nt=net rx/tx KB/s  p=top 5 processes [name, cpu%, mem%] sorted by cpu
"""
import json
import sys
import time

import psutil

INTERVAL = 2.0
NAME_LEN = 16


class Collector:
    def __init__(self):
        self._last_t = time.monotonic()
        self._last_disk = psutil.disk_io_counters()
        self._last_net = psutil.net_io_counters()
        psutil.cpu_percent(None)
        self._procs = {}
        self._prime_procs()

    def _prime_procs(self):
        for p in psutil.process_iter(["name"]):
            try:
                p.cpu_percent(None)
                self._procs[p.pid] = p
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass

    def _top_procs(self, n=5):
        rows = []
        seen = {}
        for p in psutil.process_iter(["name", "memory_percent"]):
            proc = self._procs.get(p.pid, p)
            seen[p.pid] = proc
            try:
                cpu = proc.cpu_percent(None)
                rows.append((p.info["name"] or "?", cpu, p.info["memory_percent"] or 0.0))
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                pass
        self._procs = seen
        rows.sort(key=lambda r: (r[1], r[2]), reverse=True)
        # The TFT font is ASCII-only.
        return [[name.encode("ascii", "replace").decode()[:NAME_LEN], round(cpu, 1), round(mem, 1)]
                for name, cpu, mem in rows[:n]]

    def sample(self):
        now = time.monotonic()
        dt = max(now - self._last_t, 1e-3)
        disk = psutil.disk_io_counters()
        net = psutil.net_io_counters()
        vm = psutil.virtual_memory()
        du = psutil.disk_usage("/System/Volumes/Data")
        data = {
            "c": round(psutil.cpu_percent(None), 1),
            "r": round(vm.percent, 1),
            "ru": round((vm.total - vm.available) / 1e9, 1),
            "rt": round(vm.total / 1e9, 1),
            "d": round(du.percent, 1),
            "dr": round((disk.read_bytes - self._last_disk.read_bytes) / dt / 1e6, 2),
            "dw": round((disk.write_bytes - self._last_disk.write_bytes) / dt / 1e6, 2),
            "nr": round((net.bytes_recv - self._last_net.bytes_recv) / dt / 1e3, 1),
            "nt": round((net.bytes_sent - self._last_net.bytes_sent) / dt / 1e3, 1),
            "p": self._top_procs(),
        }
        self._last_t, self._last_disk, self._last_net = now, disk, net
        return data


def encode(data):
    return json.dumps(data, separators=(",", ":"), ensure_ascii=True)


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    c = Collector()
    n = 0
    while not count or n < count:
        time.sleep(INTERVAL)
        print(encode(c.sample()), flush=True)
        n += 1


if __name__ == "__main__":
    main()
