# Dead Cells DualSense Haptics (macOS)

Replaces Dead Cells' basic rumble with Core Haptics effects on a DualSense over Bluetooth.

- 45 hooked game events: movement, combat, loot, doors, death
- 218 weapon and skill patterns generated from the game's own combo data, one per combo strike
- Dodge roll sweeps across the controller in the direction you roll
- Slashes play on the side you're facing
- All DLC weapons included

## Requirements

- Dead Cells (Steam, macOS). Tested on v35, Steam build 23762174, all DLCs
- DualSense paired over Bluetooth (hold **PS + Create** until the light bar flashes)
- macOS 12+, Apple Silicon or Intel. Tested on an M4 Pro, macOS 27.2.1

Other game versions may rename the hooked functions. The installer checks this first and leaves the
game untouched if it doesn't match.

## Install

Quit the game and run:

```bash
curl -fsSL https://github.com/not-oleh/deadcells-dualsense-haptics/releases/latest/download/install.sh | sh
```

Then turn off vibration in the game's options.

The installer finds the game across Steam library folders, backs up `hlboot.dat` to
`haptics-mod/hlboot.dat.original`, and patches it. For a non-standard location, put
`DEAD_CELLS_DIR="/path/to/Dead Cells"` before `sh`.

Re-run it after every game update: the update restores the unpatched file.

## Uninstall

```bash
curl -fsSL https://github.com/not-oleh/deadcells-dualsense-haptics/releases/latest/download/install.sh | sh -s uninstall
```

Or run `haptics-mod/uninstall.sh`, or verify game files in Steam and delete `haptics-mod`.

## Tuning

Files in `haptics-mod/`, read at game launch:

| File | Effect |
|---|---|
| `intensity.txt` | Global strength, `0.0`–`1.0` |
| `weapons.txt` | One line per weapon: `id [>] strike1 \| strike2 ...`, each step `left,right,ms` (0–255). `>` plays toward your facing side |
| `ch-off` | Empty file: use SDL rumble instead of Core Haptics |
| `hd-on` | Empty file: enable experimental raw Bluetooth haptics |
| `haptics.log` | Controller detection and fired events |

Other effects live in `PATTERNS` in `haptics.c`, the dodge sweep in `ch_dash()` in `ch.m`. Both need
a rebuild.

## How it works

Dead Cells runs on HashLink. `dchpatch` parses `hlboot.dat` and inserts a native call at the start of
each function listed in `events.def` (`Hero.onDie`, `DodgeRoll.onStart`, `Weapon.onExecute`, ...).
Weapon hooks also pass the item ID and combo step. The calls land in `haptics.hdll`, which the game
loads like its own `sdl.hdll`, and which plays patterns through GameController + Core Haptics,
falling back to SDL rumble if Core Haptics is unavailable.

| File | Role |
|---|---|
| `dchpatch.c` | Bytecode patcher |
| `events.def` | Game function → event map, shared by patcher and library |
| `haptics.c` | Patterns, priorities, `weapons.txt` loader |
| `ch.m` | Core Haptics output |
| `gen_weapons.py`, `pak.py` | Regenerate `weapons.txt` from `res.pak` (overwrites edits) |
| `hd.c` | Raw Bluetooth HD haptics (report `0x32`) |

New effect: add a line to `events.def` and a pattern to `PATTERNS`, then rebuild.

### Build

Needs Xcode Command Line Tools. The repo must sit in the game folder as `haptics-mod`, since the
build links the game's SDL:

```bash
cd ~/Library/Application\ Support/Steam/steamapps/common/Dead\ Cells
git clone https://github.com/not-oleh/deadcells-dualsense-haptics haptics-mod
cd haptics-mod && ./build.sh && ./install.sh
```

`build.sh` produces `dist/dead-cells-haptics.zip` (the release package); `install.sh` uses it
instead of downloading.

### Raw Bluetooth HD haptics

`hd.c` streams audio-rate data straight to the voice coils. It's off by default: macOS only gives it
enough bandwidth while the game window is focused, and it feels choppier than Core Haptics. Protocol
from [SAxense](https://github.com/egormanga/SAxense) and [DS5Dongle](https://github.com/awalol/DS5Dongle).

## Notes

- Keep `hlboot.dat.original` until you uninstall.
- macOS Steam version only.
- Not affiliated with Motion Twin, Evil Empire or Sony. No game files are included.
