"""Build the menu bar app:  .venv/bin/python setup.py py2app   ->  dist/µMonitor.app"""
import os
import sys

from setuptools import setup

sys.path.insert(0, "host")


def content_files():
    """Bundle content/ (themes, motivation pictures, sample photo) as Contents/Resources/content/..."""
    out = []
    for root, _, files in os.walk("content"):
        files = [os.path.join(root, f) for f in files if not f.startswith(".")]
        if files:
            out.append((root, files))
    return out

setup(
    app=["host/menubar.py"],
    name="µMonitor",
    data_files=[("", ["host/resources/menubar.png", "host/resources/menubar@2x.png"])] + content_files(),
    options={
        "py2app": {
            "iconfile": "host/resources/AppIcon.icns",
            "includes": ["collector", "sender", "content", "timers"],
            "packages": ["psutil", "serial", "rumps", "PIL", "pillow_heif"],
            "plist": {
                "CFBundleName": "µMonitor",
                "CFBundleDisplayName": "µMonitor",
                "CFBundleIdentifier": "com.xenon-feather-tft.micromonitor",
                "CFBundleShortVersionString": "1.0.0",
                "LSUIElement": True,
            },
        }
    },
)
