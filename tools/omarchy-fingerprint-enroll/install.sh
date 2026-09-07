#!/bin/bash
# Install (or refresh) the enrolment overlay into the user's Omarchy plugins
# and enable it. Saving files under ~/.config/omarchy/plugins hot-reloads
# the shell, so this is also the dev loop.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ID=$(jq -r .id "$HERE/manifest.json")
DEST=$HOME/.config/omarchy/plugins/$ID

omarchy-plugin-validate "$HERE"
mkdir -p "$DEST"
cp "$HERE"/manifest.json "$HERE"/Enroll.qml "$HERE"/FingerprintGlyph.qml "$HERE"/EnrollModel.js "$DEST"/
omarchy-shell -q shell rescanPlugins
omarchy-shell -q shell setPluginEnabled "$ID" true
echo "installed $ID -> $DEST"
echo "try:  omarchy-shell shell summon $ID '{\"finger\":\"right-index-finger\"}'"
