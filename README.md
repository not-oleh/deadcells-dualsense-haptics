# Dead Cells — DualSense Haptics for macOS

Haptic feedback for Dead Cells on a PS5 DualSense controller on macOS.

The game only has basic vibration. This mod adds haptic feedback to around 50 actions, gives
every weapon and skill its own pattern (with different hits within a combo), and plays them
through Apple's Core Haptics. It works over Bluetooth.

## What you feel

| Action | Feel |
|---|---|
| **Dodge roll** | A dash that travels across the controller: start grip → middle → end grip, in the direction you roll |
| **Weapons & skills** | ~220 patterns built from the game's own combo data: power, wind-up and recovery of each strike. Finishers hit harder; slashes play on the side you face |
| **Bows & crossbows** | A snap for light bows, a heavy kick for crossbows, a blast for explosive ones, and a tick when you start charging |
| **Movement** | Footstep taps, jump, double jump, drop-through, landing (soft and hard), ledge grab, ladders, water |
| **Combat** | Hits, crits, kills, boss kills, parries, perfect dodges, taking damage, dive-attack slam, curses, death |
| **Loot & world** | Gold and cells, items, blueprints, boss cells, chests, shop purchases, doors, teleporters, healing flask |

DLC weapons are included (Rise of the Giant, The Bad Seed, Fatal Falls, The Queen and the Sea,
Return to Castlevania).

## Tested with

- **Game:** Dead Cells (Steam), v35 (2026-06-16 - 8b5231c - 31 - Steam), Steam build 23762174 (updated 2026-11-13), all DLCs installed
- **Controller:** Sony DualSense Wireless Controller (PS5), connected over Bluetooth
- **Mac:** MacBook Pro, Apple M4 Pro, macOS 27.2.1

Needs macOS 12 or newer, on Apple Silicon or Intel. A **different game version** may move or rename
the functions this mod hooks. The installer then stops with "this Dead Cells version isn't supported
yet" and leaves the game untouched.

## Install

1. Pair the DualSense in **System Settings › Bluetooth**: hold **PS + Create** until the light bar flashes.
2. Quit Dead Cells, open **Terminal** and paste:

   ```bash
   curl -fsSL https://github.com/not-oleh/deadcells-dualsense-haptics/releases/latest/download/install.sh | sh
   ```

3. Start Dead Cells from Steam. In the game's options, **turn off the in-game vibration**:
   the mod replaces it.

No developer tools needed. The installer finds your Dead Cells folder (other Steam library drives
too), backs up the original game file to `Dead Cells/haptics-mod/hlboot.dat.original`, patches the
game and copies the mod in. Steam installed somewhere unusual? Add
`DEAD_CELLS_DIR="/path/to/Dead Cells"` in front of `sh`.

**After every Dead Cells update**, run the same command again. The update brings back the
unpatched game file, and the installer takes it as the new backup.

## Uninstall

```bash
curl -fsSL https://github.com/not-oleh/deadcells-dualsense-haptics/releases/latest/download/install.sh | sh -s uninstall
```

Or run `Dead Cells/haptics-mod/uninstall.sh`, or use Steam › Dead Cells › Properties ›
Installed Files › **Verify integrity of game files** and delete the `haptics-mod` folder.

## Tuning

All of these files are in `haptics-mod/`. Changes apply the next time you start the game.

| Want to… | Do this |
|---|---|
| Make everything weaker | Put a number from `0.0` to `1.0` in `intensity.txt` (e.g. `0.6`) |
| Change one weapon or skill | Edit its line in `weapons.txt`: one pattern per combo strike, separated by `\|`. Each step is `left,right,ms` (strength 0–255); `>` = play on the side you face |
| Change the dodge dash | Timings are in `ch_dash()` in `ch.m` ([build from source](#build-from-source)) |
| Change other actions | Edit `PATTERNS` in `haptics.c` ([build from source](#build-from-source)) |
| Go back to classic rumble | Create an empty file named `ch-off` |
| See what's firing | Check `haptics.log` (controller found, first hits of each event, weapons used) |

## How it works

Dead Cells runs on the HashLink VM. `dchpatch` reads the game's bytecode (`hlboot.dat`) and inserts
a call at the start of about 45 game functions: `Hero.onDie`, `DodgeRoll.onStart`, `ShopBooth.buy`,
`Weapon.onExecute`, and so on. Each call goes into a small native library, `haptics.hdll`, which
loads the same way the game's own `sdl.hdll` does. For weapons and skills, the injected code also
passes the item's ID and combo step, so every weapon can feel different.

`haptics.hdll` plays the effects through Apple's **GameController + Core Haptics**. Most effects
play on both grips, centred, with sharpness matched to how heavy the pattern is. Directional
effects play on one side or sweep across. Very short ones are crisp taps. If Core Haptics isn't
available, it falls back to the game's own SDL rumble.

| File | Role |
|---|---|
| `dchpatch.c` | HashLink bytecode reader/patcher: finds the hooked functions by name and inserts the calls |
| `events.def` | Which game function triggers which effect (shared by the patcher and the library) |
| `haptics.c` | The native library: effect patterns, priorities (footsteps never cut off a death), `weapons.txt` loader |
| `ch.m` | Core Haptics output, including the dodge dash |
| `gen_weapons.py`, `pak.py` | Regenerate `weapons.txt` from the game's item database in `res.pak` (overwrites your edits) |
| `hd.c` | Experimental raw Bluetooth HD haptics (see below) |

To add an effect: add a line to `events.def` and a pattern to `PATTERNS` in `haptics.c`, then build.

### Build from source

Needs Xcode Command Line Tools (`xcode-select --install`). Clone the repo into the game folder as
`haptics-mod` (the build links the game's own SDL library), then build and install:

```bash
cd ~/Library/Application\ Support/Steam/steamapps/common/Dead\ Cells
git clone https://github.com/not-oleh/deadcells-dualsense-haptics haptics-mod
cd haptics-mod && ./build.sh && ./install.sh
```

`build.sh` writes `dist/dead-cells-haptics.zip`, the same package the GitHub release ships.
`install.sh` uses those local files instead of downloading.

### Experimental: raw Bluetooth HD haptics

`hd.c` streams audio-rate haptics straight to the DualSense's voice coils over Bluetooth (output
report `0x32`). It works, but on macOS it only gets enough Bluetooth bandwidth while the game window
is focused, and in testing it felt choppier than Core Haptics. It's **off** by default. Create an
empty `hd-on` file to try it. The protocol comes from the reverse-engineering work in
[SAxense](https://github.com/egormanga/SAxense) and [DS5Dongle](https://github.com/awalol/DS5Dongle).

## Notes

- This mod changes a game file. Keep `hlboot.dat.original` until you uninstall.
- Only tested on macOS with the Steam version. Windows and Linux builds of the game are not supported.
- Not affiliated with or endorsed by Motion Twin, Evil Empire or Sony. Dead Cells and DualSense
  are trademarks of their respective owners. No game files are included in this repository.
