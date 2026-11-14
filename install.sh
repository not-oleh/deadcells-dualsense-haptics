#!/bin/sh
# Dead Cells DualSense haptics for macOS: installer.
#   curl -fsSL https://github.com/not-oleh/deadcells-dualsense-haptics/releases/latest/download/install.sh | sh
#   ... | sh -s uninstall
#   install.sh where      prints the Dead Cells folder (used by build.sh)
# Uses the prebuilt files next to this script if present (unzipped release or ./build.sh output),
# otherwise downloads the latest release. Set DEAD_CELLS_DIR to point at an unusual game folder.
set -e
REPO=not-oleh/deadcells-dualsense-haptics
say() { printf '%s\n' "$*"; }
fail() { printf 'error: %s\n' "$*" >&2; exit 1; }

[ "$(uname)" = Darwin ] || fail "this mod is for macOS only"
[ "$(sw_vers -productVersion | cut -d. -f1)" -ge 12 ] || fail "needs macOS 12 or newer"

find_game() {
  [ -n "$DEAD_CELLS_DIR" ] && { echo "$DEAD_CELLS_DIR"; return; }
  steam="$HOME/Library/Application Support/Steam/steamapps"
  # default library first, then any other Steam library folders (external drives etc.)
  { echo "$steam"; sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"/\1\/steamapps/p' "$steam/libraryfolders.vdf" 2>/dev/null; } |
  while IFS= read -r lib; do
    [ -f "$lib/common/Dead Cells/hlboot.dat" ] && { echo "$lib/common/Dead Cells"; break; }
  done
}
GAME=$(find_game)
[ -n "$GAME" ] && [ -f "$GAME/hlboot.dat" ] || fail "Dead Cells not found. Install it from Steam, or run with DEAD_CELLS_DIR=/path/to/Dead\ Cells"
[ "$1" = where ] && { echo "$GAME"; exit 0; }
MOD="$GAME/haptics-mod"
pgrep -f "Dead Cells/deadcells" >/dev/null && fail "Dead Cells is running. Quit the game first, then run this again."

if [ "$1" = uninstall ]; then
  [ -f "$MOD/hlboot.dat.original" ] || fail "no backup found. Use Steam > Dead Cells > Properties > Installed Files > Verify integrity."
  cp "$MOD/hlboot.dat.original" "$GAME/hlboot.dat"
  rm -f "$GAME/haptics.hdll"
  [ -d "$MOD/.git" ] || rm -rf "$MOD" # keep a developer's git checkout
  say "Uninstalled. Dead Cells is back to the original."
  exit 0
fi

# get the prebuilt files
SRC=""
here=$(cd "$(dirname "$0")" 2>/dev/null && pwd)
for d in "$here" "$here/dist/pkg"; do
  [ -f "$d/dchpatch" ] && [ -f "$d/haptics.hdll" ] && { SRC="$d"; break; }
done
if [ -z "$SRC" ]; then
  TMP=$(mktemp -d)
  trap 'rm -rf "$TMP"' EXIT
  say "Downloading the latest release..."
  curl -fsSL -o "$TMP/mod.zip" "https://github.com/$REPO/releases/latest/download/dead-cells-haptics.zip" || fail "download failed"
  unzip -q "$TMP/mod.zip" -d "$TMP/pkg"
  SRC="$TMP/pkg"
fi

say "Installing into: $GAME"
mkdir -p "$MOD"
copy() { [ "$1/$2" -ef "$MOD/$2" ] || cp "$1/$2" "$MOD/$2"; }
copy "$SRC" dchpatch
copy "$SRC" uninstall.sh
if [ -f "$MOD/weapons.txt" ] && ! cmp -s "$SRC/weapons.txt" "$MOD/weapons.txt"; then
  cp "$MOD/weapons.txt" "$MOD/weapons.txt.bak" # keep the player's tuning
fi
copy "$SRC" weapons.txt
chmod +x "$MOD/dchpatch" "$MOD/uninstall.sh"

# back up the game's original file (also after a game update, which brings back an unpatched one)
state=$("$MOD/dchpatch" status "$GAME/hlboot.dat") || fail "can't read the game's hlboot.dat (unsupported game version?)"
if [ "$state" = 0 ]; then
  cp "$GAME/hlboot.dat" "$MOD/hlboot.dat.original"
elif [ ! -f "$MOD/hlboot.dat.original" ]; then
  fail "the game is already patched but the backup is missing. Verify game files in Steam, then run this again."
fi
# patch into a temp file first: on failure the game stays untouched
"$MOD/dchpatch" patch "$MOD/hlboot.dat.original" "$GAME/hlboot.dat.tmp" >/dev/null ||
  { rm -f "$GAME/hlboot.dat.tmp"; fail "this Dead Cells version isn't supported yet. The game was not changed."; }
mv "$GAME/hlboot.dat.tmp" "$GAME/hlboot.dat"
[ "$SRC/haptics.hdll" -ef "$GAME/haptics.hdll" ] || cp "$SRC/haptics.hdll" "$GAME/haptics.hdll"

say ""
say "Done! Start Dead Cells from Steam."
say "Tip: turn off the in-game vibration (Options > Controller), the mod replaces it."
say "After a Dead Cells update, run the same install command again."
say "Uninstall: run the install command with 'sh -s uninstall', or $MOD/uninstall.sh"
