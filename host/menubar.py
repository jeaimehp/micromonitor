#!/usr/bin/env python3
"""µMonitor menu bar app: streams this Mac's metrics, pictures and themes to the Xenon dashboard, mirrors the
display's touch menu, and runs a timer / stopwatch shown on the display."""
import os
import plistlib
import subprocess
import sys
import threading

import rumps

import content
from sender import Streamer

APP_NAME = "µMonitor"  # micro sign U+00B5
LABEL = "com.xenon-feather-tft.micromonitor"
LAUNCH_AGENT = os.path.expanduser(f"~/Library/LaunchAgents/{LABEL}.plist")
HERE = os.path.dirname(os.path.abspath(__file__))

# Mirrors of the device menu; the device reports each setting as the index of its option.
VIEWS = ["Dashboard", "Album", "Mixed"]
LAYOUTS = ["Quad", "Stacked", "Focus", "Tiles", "LCARS", "Tron"]
FOLDERS = ["Photos", "Motivation"]
SLIDES = ["Every 5 seconds", "Every 10 seconds", "Every 30 seconds", "Every minute", "Every 5 minutes",
          "Every 30 minutes"]
SLIDE_CUSTOM = len(SLIDES)  # the device's index for the custom interval
ROTATIONS = ["Landscape", "Landscape flipped 180°", "Portrait", "Portrait flipped 180°"]
SIDES = ["Photo on the Left", "Photo on the Right"]
TIMER_PRESETS = [1, 5, 10, 15, 25, 60]


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


def osascript(script):
    """Run AppleScript; returns stdout, or None if the user cancelled."""
    r = subprocess.run(["osascript", "-e", script], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


class DashboardApp(rumps.App):
    def __init__(self):
        super().__init__(APP_NAME, icon=resource("menubar.png"), template=True, quit_button=None)
        self.status_item = rumps.MenuItem("Starting...")
        self.detail_item = rumps.MenuItem("")
        self.timer_status = rumps.MenuItem("")
        self.stream_item = rumps.MenuItem("Streaming", callback=self.toggle_streaming)
        self.cpu_item = rumps.MenuItem("Show CPU % in Menu Bar", callback=self.toggle_cpu)
        self.login_item = rumps.MenuItem("Start at Login", callback=self.toggle_login)

        # Display: mirrors the touch menu on the screen.
        self.display = rumps.MenuItem("Display")
        self.choice_menus = {}
        for title, key, labels in [("View", "view", VIEWS), ("Layout", "layout", LAYOUTS),
                                   ("Album Pictures", "folder", FOLDERS), ("Slideshow", "slides", SLIDES),
                                   ("Rotation", "rot", ROTATIONS), ("Mixed View", "side", SIDES)]:
            menu = self._choice_menu(title, key, labels)
            if key == "slides":
                menu.add(None)
                self.slide_custom_item = rumps.MenuItem("Custom…", callback=self.custom_slideshow)
                menu.add(self.slide_custom_item)
            self.display.add(menu)
        self.theme_menu = rumps.MenuItem("Theme")
        self.theme_items = []
        self.display.add(self.theme_menu)
        self.badge_item = rumps.MenuItem("Clock on Pictures", callback=self.toggle_badge)
        self.display.add(self.badge_item)
        self.lphoto_item = rumps.MenuItem("Photo in LCARS Layout",
                                          callback=lambda item: self.send(f"lphoto {0 if item.state else 1}"))
        self.display.add(self.lphoto_item)
        self.display.add(None)
        self.display.add(rumps.MenuItem("Next Picture", callback=lambda _: self.send("next")))
        self.display.add(rumps.MenuItem("Previous Picture", callback=lambda _: self.send("prev")))
        self.display.add(None)
        self.display.add(rumps.MenuItem("Calibrate Touch…", callback=self.calibrate))

        pictures = rumps.MenuItem("Pictures")
        pictures.add(rumps.MenuItem("Add Pictures…", callback=self.add_pictures))
        pictures.add(rumps.MenuItem("Choose Photos Folder…", callback=self.choose_folder))
        pictures.add(rumps.MenuItem("Open Photos Folder", callback=self.open_folder))
        pictures.add(None)
        self.hold_item = rumps.MenuItem("Hold Current Photo",
                                        callback=lambda item: self.send(f"hold {0 if item.state else 1}"))
        pictures.add(self.hold_item)
        pictures.add(rumps.MenuItem("Next Picture", callback=lambda _: self.send("next")))

        timer = rumps.MenuItem("Timer")
        for m in TIMER_PRESETS:
            timer.add(rumps.MenuItem(f"{m} minute{'s' if m > 1 else ''}", callback=self._preset(m)))
        timer.add(rumps.MenuItem("Custom…", callback=self.custom_timer))
        timer.add(None)
        timer.add(rumps.MenuItem("Pause / Resume", callback=lambda _: self._timers("toggle_pause")))
        timer.add(rumps.MenuItem("Cancel", callback=lambda _: self._timers("cancel")))

        stopwatch = rumps.MenuItem("Stopwatch")
        stopwatch.add(rumps.MenuItem("Start / Stop", callback=lambda _: self._timers("stopwatch_start_stop")))
        stopwatch.add(rumps.MenuItem("Reset", callback=lambda _: self._timers("stopwatch_reset")))

        self.menu = [
            self.status_item, self.detail_item, self.timer_status, None,
            self.display, pictures, timer, stopwatch, None,
            self.stream_item, self.cpu_item, self.login_item, None,
            rumps.MenuItem("Quit", callback=self.quit),
        ]
        self.cpu_item.state = 1
        self.login_item.state = int(os.path.exists(LAUNCH_AGENT))
        self.streamer = None
        self.thread = None
        self.start_streaming()
        self.refresh_timer = rumps.Timer(self.refresh, 1)
        self.refresh_timer.start()

    # ---- display settings (sent as "cmd <key> <value>"; the device answers with its new state) ----
    def _choice_menu(self, title, key, labels):
        menu = rumps.MenuItem(title)
        items = []
        for i, label in enumerate(labels):
            item = rumps.MenuItem(label, callback=lambda _, k=key, v=i: self.send(f"{k} {v}"))
            menu.add(item)
            items.append(item)
        self.choice_menus[key] = items
        return menu

    def send(self, text):
        if not (self.streamer and self.stream_item.state and self.streamer.command(text)):
            rumps.notification(APP_NAME, "Display not connected", "Plug in the Xenon and turn on Streaming.")
            return False
        return True

    def toggle_badge(self, item):
        self.send(f"badge {0 if item.state else 1}")

    def calibrate(self, _):
        if self.send("calibrate"):
            rumps.notification(APP_NAME, "Calibrate the touch screen",
                               "Press the center of each target on the display, one at a time.")

    def custom_slideshow(self, _):
        current = self.streamer.device_state.get("slidecustom", 120) if self.streamer else 120
        w = rumps.Window("Show each picture for how many minutes? (decimals OK, e.g. 0.5 = 30 seconds)",
                         "Custom Slideshow", default_text=f"{current / 60:g}", ok="Set", cancel="Cancel",
                         dimensions=(200, 24))
        r = w.run()
        if not r.clicked:
            return
        try:
            seconds = round(float(r.text.strip()) * 60)
        except ValueError:
            return
        if 1 <= seconds <= 65535:
            self.send(f"slidecustom {seconds}")
        else:
            rumps.notification(APP_NAME, "Slideshow time out of range", "Use between 1 second and 18 hours.")

    @staticmethod
    def _duration(seconds):
        m, s = divmod(int(seconds), 60)
        return " ".join(p for p in (f"{m} min" if m else "", f"{s} s" if s else "") if p) or "0 s"

    def _rebuild_themes(self, names):
        if [i.title for i in self.theme_items] == names:
            return
        if self.theme_items:
            self.theme_menu.clear()
        self.theme_items = []
        for i, name in enumerate(names):
            item = rumps.MenuItem(name, callback=lambda _, v=i: self.send(f"theme {v}"))
            self.theme_menu.add(item)
            self.theme_items.append(item)

    # ---- pictures ----
    def add_pictures(self, _):
        out = osascript('set fs to choose file with prompt "Add pictures to µMonitor" of type {"public.image"} '
                        'with multiple selections allowed\n'
                        'set out to ""\nrepeat with f in fs\nset out to out & POSIX path of f & linefeed\nend repeat\n'
                        'return out')
        if out:
            n = content.add_pictures([p for p in out.splitlines() if p])
            rumps.notification(APP_NAME, f"Added {n} picture{'s' if n != 1 else ''}", content.photos_dir())
            if self.streamer:
                self.streamer.command("next")  # show a new picture if the album is open

    def choose_folder(self, _):
        out = osascript('return POSIX path of (choose folder with prompt "Choose the folder with your photos")')
        if out:
            content.set_photos_dir(out.rstrip("/"))
            if self.streamer:
                self.streamer.command("next")

    def open_folder(self, _):
        subprocess.run(["open", content.photos_dir()])

    # ---- timer / stopwatch (kept on the Mac, shown on the display) ----
    def _timers(self, action, *args):
        if self.streamer:
            getattr(self.streamer.timers, action)(*args)
            self.refresh(None)

    def _preset(self, minutes):
        return lambda _: self._timers("start_timer", minutes * 60)

    def custom_timer(self, _):
        w = rumps.Window("Minutes (decimals OK, e.g. 2.5):", "Custom Timer", default_text="20",
                         ok="Start", cancel="Cancel", dimensions=(200, 24))
        r = w.run()
        if r.clicked:
            try:
                minutes = float(r.text.strip())
            except ValueError:
                return
            if minutes > 0:
                self._timers("start_timer", minutes * 60)

    # ---- streaming ----
    def start_streaming(self):
        old = self.streamer.timers if self.streamer else None
        self.streamer = Streamer()
        if old:
            self.streamer.timers = old  # keep a running timer across pausing and resuming the stream
        self.thread = threading.Thread(target=self.streamer.run, daemon=True)
        self.thread.start()
        self.stream_item.state = 1

    def stop_streaming(self):
        if self.streamer:
            self.streamer.stop()
            self.thread.join(timeout=3)
        self.stream_item.state = 0

    def toggle_streaming(self, _):
        if self.stream_item.state:
            self.stop_streaming()
        else:
            self.start_streaming()
        self.refresh(None)

    # ---- UI ----
    def refresh(self, _):
        s = self.streamer
        if s and s.timers.consume_done():
            rumps.notification(APP_NAME, "Time's up!", "Your timer has finished.")
        tdesc = s.timers.describe() if s else None
        self.timer_status.title = tdesc or "No timer running"
        if not self.stream_item.state:
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
        # Check marks follow the device's reported state.
        st = s.device_state
        for key, items in self.choice_menus.items():
            for i, item in enumerate(items):
                item.state = int(st.get(key) == i)
        self._rebuild_themes(s.device_themes)
        for i, item in enumerate(self.theme_items):
            item.state = int(st.get("theme") == i)
        self.badge_item.state = int(st.get("badge", 0) == 1)
        self.lphoto_item.state = int(st.get("lphoto", 0) == 1)
        self.hold_item.state = int(st.get("hold", 0) == 1)
        custom = st.get("slides") == SLIDE_CUSTOM
        self.slide_custom_item.state = int(custom)
        self.slide_custom_item.title = (f"Custom ({self._duration(st.get('slidecustom', 0))})…" if custom else "Custom…")
        # Menu bar title: a running timer wins over CPU %.
        if tdesc and s.connected_port:
            self.title = tdesc.split(": ", 1)[1]
        elif self.cpu_item.state and s.connected_port and s.last_sample:
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
