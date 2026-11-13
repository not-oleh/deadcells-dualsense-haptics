#!/bin/sh
set -e
cd "$(dirname "$0")"
[ -f hlboot.dat.original ] || { echo "no backup found: use Steam > Dead Cells > Properties > Installed Files > Verify integrity"; exit 1; }
cp hlboot.dat.original ../hlboot.dat
rm -f ../haptics.hdll
echo "restored the original game. You can delete this folder now."
