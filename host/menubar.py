#!/usr/bin/env python3
"""Menu bar app: streams this Mac's metrics to the Xenon dashboard and shows the link status as a status icon."""
import os
import plistlib
import subprocess
import sys
import threading

import rumps

from sender import Streamer

APP_NAME = "µMonitor"  # micro sign U+00B5
LABEL = "com.xenon-feather-tft.micromonitor"
LAUNCH_AGENT = os.path.expanduser(f"~/Library/LaunchAgents/{LABEL}.plist")
HERE = os.path.dirname(os.path.abspath(__file__))


def resource(name):
    """Path to a bundled resource (py2app puts them in Contents/Resources, the cwd of a frozen app)."""
    if getattr(sys, "frozen", False):
        return os.path.join(os.environ.get("RESOURCEPATH", ""), name)
    return os.path.join(HERE, "resources", name)


def launch_command():
    """Command that starts this app: the bundle executable when frozen, otherwise this script."""
    if getattr(sys, "frozen", False):
        return [os.path.join(os.environ["RESOURCEPATH"], "..", "MacOS", APP_NAME)]
    return [sys.executable, os.path.abspath(__file__)]


class DashboardApp(rumps.App):
    def __init__(self):
        super().__init__(APP_NAME, icon=resource("menubar.png"), template=True, quit_button=None)
        self.status_item = rumps.MenuItem("Starting...")
        self.detail_item = rumps.MenuItem("")
        self.stream_item = rumps.MenuItem("Streaming", callback=self.toggle_streaming)
        self.cpu_item = rumps.MenuItem("Show CPU % in Menu Bar", callback=self.toggle_cpu)
        self.login_item = rumps.MenuItem("Start at Login", callback=self.toggle_login)
        self.menu = [
            self.status_item,
            self.detail_item,
            None,
            self.stream_item,
            self.cpu_item,
            self.login_item,
            None,
            rumps.MenuItem("Quit", callback=self.quit),
        ]
        self.cpu_item.state = 1
        self.login_item.state = int(os.path.exists(LAUNCH_AGENT))
        self.streamer = None
        self.thread = None
        self.start_streaming()
        self.timer = rumps.Timer(self.refresh, 1)
        self.timer.start()

    # ---- streaming ----
    def start_streaming(self):
        self.streamer = Streamer()
        self.thread = threading.Thread(target=self.streamer.run, daemon=True)
        self.thread.start()
        self.stream_item.state = 1

    def stop_streaming(self):
        if self.streamer:
            self.streamer.stop()
            self.thread.join(timeout=3)
        self.streamer = None
        self.stream_item.state = 0

    def toggle_streaming(self, _):
        if self.streamer:
            self.stop_streaming()
        else:
            self.start_streaming()
        self.refresh(None)

    # ---- UI ----
    def refresh(self, _):
        s = self.streamer
        if s is None:
            self.status_item.title = "Paused"
            self.detail_item.title = "Dashboard shows NO HOST DATA"
            self.title = None
            return
        if s.connected_port:
            self.status_item.title = f"Connected: {os.path.basename(s.connected_port)}"
            ack = s.last_ack if s.last_ack.startswith("ack") else ""
            draw = next((f.split("=")[1] for f in ack.split() if f.startswith("draw=")), None)
            self.detail_item.title = f"{s.sent} samples sent" + (f" · draw {draw}" if draw else "")
        elif s.open_error:
            self.status_item.title = "Xenon port busy (is run.sh running?)"
            self.detail_item.title = "Retrying every 2s"
        else:
            self.status_item.title = "Xenon not found (plug in USB)"
            self.detail_item.title = "Retrying every 2s"
        if self.cpu_item.state and s.connected_port and s.last_sample:
            self.title = f"{s.last_sample['c']:.0f}%"
        else:
            self.title = None if s.connected_port else "!"

    def toggle_cpu(self, item):
        item.state = int(not item.state)
        self.refresh(None)

    # ---- login item (per-user LaunchAgent) ----
    def toggle_login(self, item):
        domain = f"gui/{os.getuid()}"
        if item.state:
            subprocess.run(["launchctl", "bootout", f"{domain}/{LABEL}"], capture_output=True)
            if os.path.exists(LAUNCH_AGENT):
                os.remove(LAUNCH_AGENT)
            item.state = 0
            return
        os.makedirs(os.path.dirname(LAUNCH_AGENT), exist_ok=True)
        with open(LAUNCH_AGENT, "wb") as f:
            plistlib.dump({
                "Label": LABEL,
                "ProgramArguments": [os.path.normpath(p) for p in launch_command()],
                "RunAtLoad": True,
                "LimitLoadToSessionType": "Aqua",
            }, f)
        item.state = 1

    def quit(self, _):
        self.stop_streaming()
        rumps.quit_application()


if __name__ == "__main__":
    DashboardApp().run()
