"""Build the menu bar app:  .venv/bin/python setup.py py2app   ->  dist/µMonitor.app"""
import sys

from setuptools import setup

sys.path.insert(0, "host")

setup(
    app=["host/menubar.py"],
    name="µMonitor",
    data_files=[("", ["host/resources/menubar.png", "host/resources/menubar@2x.png"])],
    options={
        "py2app": {
            "includes": ["collector", "sender"],
            "packages": ["psutil", "serial", "rumps"],
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
