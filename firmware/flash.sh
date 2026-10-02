#!/bin/bash
# Usage: firmware/flash.sh <project-dir>   (e.g. firmware/hello)
# Cloud-compiles the project for Xenon / Device OS 1.5.2 and flashes it over USB.
set -euo pipefail
P=${PARTICLE:-/Users/jeaimehp/.hermes/node/bin/particle}
DIR=${1:?project dir}
OUT=$(mktemp -t xenon).bin
(cd "$DIR" && "$P" compile xenon --target 1.5.2 --saveTo "$OUT" | grep -E "succeeded|error|Flash|RAM|[0-9]+ +[0-9]+" )
"$P" flash --local --application-only "$OUT" | tail -1
rm -f "$OUT"
