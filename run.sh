#!/bin/bash
# Start the host side of the dashboard: samples this Mac every 2s and streams to the Xenon over USB.
cd "$(dirname "$0")"
exec .venv/bin/python host/sender.py "$@"
