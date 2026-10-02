#!/bin/bash
# Usage: tools/snap.sh <out.jpg>   Photograph the TFT with the C920 webcam (resized to 1000px).
set -euo pipefail
OUT=${1:?output jpg}
imagesnap -d "HD Pro Webcam C920" -w 2 "$OUT" >/dev/null
sips -Z 1000 "$OUT" >/dev/null
echo "$OUT"
