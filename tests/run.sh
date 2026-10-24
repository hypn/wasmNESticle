#!/bin/bash
# Builds both targets and runs the headless tests. Output PNGs go to tests/out/.
set -euo pipefail
cd "$(dirname "$0")/.."
out=tests/out; mkdir -p "$out"
python3 -I tests/make_testrom.py "$out/test.nes"
./build.sh node
node tests/render_png.js "$out/ui.png" 5 "move:30:4,down:1,frames:3,shot:$out/ui_menu.png"
node tests/render_png.js "$out/rom.png" 3 "rom:$out/test.nes,frames:30"
node tests/rom_features.js "$out/test.nes" "$out"
node tests/dialog_flow.js "$out/test.nes" "$out"
node tests/keys_dialog.js "$out"
./build.sh web
node tests/audio.js "$out"
node tests/hotkeys.js
node tests/web_glue_test.js "$out/test.nes" "$out" "$out/audio_pulse.nes"
echo "all tests passed"
