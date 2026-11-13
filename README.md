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

Other Macs and macOS versions with DualSense support in System Settings should work. A
**different game version** may move or rename the functions this mod hooks. The installer then
stops with `function not found: …` instead of breaking the game.

## Install

You need Xcode Command Line Tools (`clang`, `python3`). If you don't have them:

```bash
xcode-select --install
```

1. Pair the DualSense in **System Settings › Bluetooth**: hold **PS + Create** until the light bar flashes.
2. Download the mod into the game folder as `haptics-mod`:

   ```bash
   cd ~/Library/Application\ Support/Steam/steamapps/common/Dead\ Cells
   git clone https://github.com/not-oleh/deadcells-dualsense-haptics haptics-mod
   ```

3. Install:

   ```bash
   ./haptics-mod/install.sh
   ```

4. Start Dead Cells from Steam. In the game's options, **turn off the in-game vibration**:
   it would fight with these effects.

`install.sh` backs up the game's original `hlboot.dat` to `haptics-mod/hlboot.dat.original`,
patches the game and builds `haptics.hdll` next to it.

**After every Dead Cells update**, run `./haptics-mod/install.sh` again. The update replaces the
patched file, and the script takes the new version as the backup.

## Uninstall

```bash
./haptics-mod/uninstall.sh
```

Or use Steam › Dead Cells › Properties › Installed Files › **Verify integrity of game files**.
Then delete the `haptics-mod` folder.

## Tuning

All of these files are in `haptics-mod/`. Changes apply the next time you start the game.

| Want to… | Do this |
|---|---|
| Make everything weaker | Put a number from `0.0` to `1.0` in `intensity.txt` (e.g. `0.6`) |
| Change one weapon or skill | Edit its line in `weapons.txt`: one pattern per combo strike, separated by `\|`. Each step is `left,right,ms` (strength 0–255); `>` = play on the side you face |
| Change the dodge dash | Timings are in `ch_dash()` in `ch.m`, then run `install.sh` |
| Change other actions | Edit `PATTERNS` in `haptics.c`, then run `install.sh` |
| Go back to classic rumble | Create an empty file named `ch-off` |
| See what's firing | Check `haptics.log` (controller found, first hits of each event, weapons used) |

## How it works

Dead Cells runs on the HashLink VM. `patch.py` reads the game's bytecode (`hlboot.dat`) and inserts
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
| `patch.py`, `hlbc.py` | HashLink bytecode reader/patcher. The `EVENTS` list says which game functions trigger which effect |
| `haptics.c` | The native library: effect patterns, priorities (footsteps never cut off a death), `weapons.txt` loader |
| `ch.m` | Core Haptics output, including the dodge dash |
| `gen_weapons.py`, `pak.py` | Regenerate `weapons.txt` from the game's item database in `res.pak` (overwrites your edits) |
| `hd.c` | Experimental raw Bluetooth HD haptics (see below) |

To add an effect: add a line to `EVENTS` in `patch.py` and a pattern to `PATTERNS` in `haptics.c`,
then run `install.sh`.

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
