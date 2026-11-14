#!/bin/sh
# Developer build: compiles dchpatch (universal) and haptics.hdll (x86_64, the game runs under Rosetta)
# into dist/ and packs dist/dead-cells-haptics.zip for a GitHub release. Needs Xcode Command Line Tools.
# SDL symbols are left unresolved: they bind at runtime to the libSDL2 the game has already loaded.
set -e
cd "$(dirname "$0")"
rm -rf dist && mkdir -p dist/pkg
clang -O2 -Wall -arch arm64 -arch x86_64 -mmacosx-version-min=12.0 -o dist/pkg/dchpatch dchpatch.c
clang -arch x86_64 -O2 -Wall -mmacosx-version-min=12.0 -dynamiclib -o dist/pkg/haptics.hdll haptics.c hd.c ch.m -fobjc-arc \
  -framework IOKit -framework CoreFoundation -framework Foundation -framework GameController -framework CoreHaptics \
  -Wl,-undefined,dynamic_lookup
# dynamic_lookup hides link errors: anything unresolved besides SDL is a typo that would crash the game
bad=$(nm -m -u dist/pkg/haptics.hdll | grep 'dynamically looked up' | grep -v ' _SDL_' || true)
[ -z "$bad" ] || { echo "error: unresolved symbols:"; echo "$bad"; exit 1; }
cp weapons.txt install.sh uninstall.sh dist/pkg/
(cd dist/pkg && zip -qr ../dead-cells-haptics.zip .)
echo "built dist/dead-cells-haptics.zip"
