#!/bin/sh
# Developer build: compiles dchpatch (universal) and haptics.hdll (x86_64, the game runs under Rosetta)
# into dist/ and packs dist/dead-cells-haptics.zip for a GitHub release. Needs Xcode Command Line Tools.
# Run it from inside the Dead Cells folder (as haptics-mod/), since it links the game's own libSDL2.
set -e
cd "$(dirname "$0")"
GAME=..
[ -f "$GAME/libSDL2-2.0.0.dylib" ] || { echo "error: run from <Dead Cells>/haptics-mod (needs the game's libSDL2)"; exit 1; }
rm -rf dist && mkdir -p dist/pkg
clang -O2 -Wall -arch arm64 -arch x86_64 -mmacosx-version-min=12.0 -o dist/pkg/dchpatch dchpatch.c
clang -arch x86_64 -O2 -Wall -mmacosx-version-min=12.0 -dynamiclib -o dist/pkg/haptics.hdll haptics.c hd.c ch.m -fobjc-arc \
  -framework IOKit -framework CoreFoundation -framework Foundation -framework GameController -framework CoreHaptics \
  -L"$GAME" -lSDL2-2.0.0 -Wl,-rpath,@loader_path
cp weapons.txt install.sh uninstall.sh dist/pkg/
(cd dist/pkg && zip -qr ../dead-cells-haptics.zip .)
echo "built dist/dead-cells-haptics.zip"
