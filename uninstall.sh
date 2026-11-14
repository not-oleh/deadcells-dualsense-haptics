#!/bin/sh
# Restores the original Dead Cells file and removes the mod. Run it from anywhere.
set -e
MOD=$(cd "$(dirname "$0")" && pwd)
GAME=$(dirname "$MOD")
pgrep -f "Dead Cells/deadcells" >/dev/null && { echo "error: quit Dead Cells first"; exit 1; }
[ -f "$MOD/hlboot.dat.original" ] || { echo "error: no backup found. Use Steam > Dead Cells > Properties > Installed Files > Verify integrity."; exit 1; }
cp "$MOD/hlboot.dat.original" "$GAME/hlboot.dat"
rm -f "$GAME/haptics.hdll"
[ -d "$MOD/.git" ] || rm -rf "$MOD" # keep a developer's git checkout
echo "Uninstalled. Dead Cells is back to the original."
