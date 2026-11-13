#!/bin/sh
# Builds haptics.hdll and patches hlboot.dat. Re-run after every Dead Cells update.
set -e
cd "$(dirname "$0")"
GAME=..

[ -f "$GAME/hlboot.dat" ] && [ -f "$GAME/libSDL2-2.0.0.dylib" ] || {
  echo "error: put this folder inside the Dead Cells game folder (next to hlboot.dat) as 'haptics-mod'"; exit 1; }
command -v clang >/dev/null && command -v python3 >/dev/null || {
  echo "error: needs clang and python3. Install them with: xcode-select --install"; exit 1; }

# 0 = original game file, 1 = already patched by this mod
state=$(python3 -c "import hlbc; c=hlbc.Code(open('$GAME/hlboot.dat','rb').read()); print(int('dch_event' in c.strings))")
if [ "$state" = 0 ]; then
  cp "$GAME/hlboot.dat" hlboot.dat.original # fresh install or game updated: back up the new original
elif [ ! -f hlboot.dat.original ]; then
  echo "error: hlboot.dat is already patched but the backup is missing. Verify game files in Steam, then re-run."; exit 1
fi
python3 patch.py
clang -arch x86_64 -O2 -dynamiclib -o "$GAME/haptics.hdll" haptics.c hd.c ch.m -fobjc-arc \
  -framework IOKit -framework CoreFoundation -framework Foundation -framework GameController -framework CoreHaptics \
  -L"$GAME" -lSDL2-2.0.0 -Wl,-rpath,@loader_path
echo "installed. Start Dead Cells from Steam. Uninstall: ./uninstall.sh"
