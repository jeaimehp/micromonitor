#!/bin/bash
# Install a per-user LaunchAgent that keeps the dashboard host running (starts at login, restarts if it exits).
# Undo with: tools/install-launchd.sh --uninstall
set -euo pipefail
LABEL=com.xenon-feather-tft.dashboard
PLIST=~/Library/LaunchAgents/$LABEL.plist
ROOT=$(cd "$(dirname "$0")/.." && pwd)

if [ "${1:-}" = "--uninstall" ]; then
    launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
    rm -f "$PLIST"
    echo "removed $PLIST"
    exit 0
fi

mkdir -p ~/Library/LaunchAgents
cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key><string>$LABEL</string>
    <key>ProgramArguments</key><array><string>$ROOT/run.sh</string></array>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
    <key>StandardOutPath</key><string>/tmp/$LABEL.log</string>
    <key>StandardErrorPath</key><string>/tmp/$LABEL.log</string>
</dict>
</plist>
PL
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "installed $PLIST (log: /tmp/$LABEL.log)"
