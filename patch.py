"""Inject `haptics.dch_event(id)` at the start of selected game functions in hlboot.dat.
Usage: python3 patch.py            (reads hlboot.dat.original, writes ../hlboot.dat)
"""
import os, sys
import hlbc
from hlbc import enc_index as E, OP

HERE = os.path.dirname(os.path.abspath(__file__))

# (event name, game function). Order = event id; haptics.c PATTERNS must match (events.h is generated).
EVENTS = [
    ("STEP",          "en.Hero.playStepSound"),
    ("JUMP",          "tool.mainSkills.Jump.onStart"),
    ("AIR_JUMP",      "tool.mainSkills.AirJump.onStart"),
    ("JUMP_DOWN",     "tool.mainSkills.JumpDown.onStart"),
    ("LAND",          "en.Hero.onLand"),
    ("LAND_HARD",     "en.Hero.landStun"),
    ("DODGE",         "tool.mainSkills.DodgeRoll.onStart"),
    ("DODGE_RICHTER", "tool.mainSkills.RichterDodge.onStart"),
    ("PERFECT_DODGE", "tool.mainSkills.Dodge.perfectDodge"),
    ("DIVE",          "tool.mainSkills.DiveAttack.onStart"),
    ("DIVE_LAND",     "tool.mainSkills.DiveAttack.onOwnerLand"),
    ("HIT_DEALT",     "en.Hero.onOwnAttackDealt"),
    ("CRIT",          "Fx.critical"),
    ("PARRY",         "en.Hero.onPerfectParry"),
    ("DAMAGED",       "en.Hero.onDamage"),
    ("FATAL_FALL",    "en.Hero.onFatalFallDamage"),
    ("HERO_DIE",      "en.Hero.onDie"),
    ("KILL",          "en.Mob.onDie"),
    ("BOSS_KILL",     "en.mob.Boss.onDie"),
    ("HEAL",          "tool.mainSkills.Heal.onStart"),
    ("FLASK",         "tool.mainSkills.HealFlask.onStart"),
    ("GOLD",          "en.Hero.addMoney"),
    ("CELLS",         "en.Hero.addCells"),
    ("BUY",           "en.inter.ShopBooth.buy"),
    ("ITEM_PICK",     "en.Hero.pickItem"),
    ("BLUEPRINT",     "en.loot.Blueprint.pickUp"),
    ("BOSS_CELL",     "en.loot.BossCell.pickUp"),
    ("CHEST",         "en.inter.TreasureChest.open"),
    ("CURSED_CHEST",  "en.inter.CursedChest.open"),
    ("DOOR_KICK",     "en.inter.Door.openWithAKick"),
    ("DOOR_BREAK",    "en.inter.Door.onDie"),
    ("BREAK_GROUND",  "en.Hero.breakBreakableGround"),
    ("TELEPORT",      "en.Hero.onTeleportation"),
    ("CLIFF_GRAB",    "en.Hero.cliffGrab"),
    ("CLIMB",         "en.Hero.startClimbing"),
    ("WATER",         "en.Hero.onEnterWater"),
    ("INTERACT",      "en.Hero.onUseInteractive"),
    ("CURSED",        "en.Hero.curse"),
    ("LEVEL_ENTER",   "en.Hero.onLevelChanged"),
    ("WAKEUP",        "en.Hero.wakeup"),
    ("BOW_CHARGE",    "tool.weap.BaseBow.onBowChargeStart"),
    ("SCEPTER_SPIN",  "tool.weap.KingScepter.jumpAndStartSpin"),
    ("SCEPTER_STOP",  "tool.weap.KingScepter.stopSpin"),
]

NATIVE_LIB = "haptics"

# hooks that pass the item id (InventItemKind's String) + combo strike to haptics.dch_item(id, strike)
ITEM_HOOKS = [
    ("weapon", "tool.hero.weap.BeheadedWeaponsManager.onWeaponExecute", 1),  # arg 1 = tool.Weapon
    ("item",   "tool.hero.activeSkills.BeheadedActiveSkillsManager.useSkillItem", 2),  # arg 2 = InventItem
]


def main():
    src = os.path.join(HERE, "hlboot.dat.original")
    dst = os.path.join(HERE, "..", "hlboot.dat")
    data = open(src, "rb").read()
    c = hlbc.Code(data)
    names = c.names()
    byname = {}
    for fi, n in names.items():
        byname.setdefault(n, fi)
    fun_by_findex = {f.findex: f for f in c.funcs}
    T = lambda pred: next(i for i, t in enumerate(c.types) if pred(t))
    named = lambda k, nm: T(lambda t: t["kind"] == k and t.get("name") == nm)
    fidx = lambda ti, nm: [n for n, _ in c._all_fields(c.types[ti])].index(nm)

    i32, void, tbytes = T(lambda t: t["kind"] == 3), T(lambda t: t["kind"] == 0), T(lambda t: t["kind"] == 8)
    t_weapon, t_item, t_str = named(11, "tool.Weapon"), named(11, "tool.InventItem"), named(11, "String")
    t_kind = named(18, "tool.InventItemKind")
    f_item, f_cycle, f_kind, f_bytes = fidx(t_weapon, "item"), fidx(t_weapon, "_cycle"), fidx(t_item, "kind"), fidx(t_str, "bytes")
    ft_event = T(lambda t: t["kind"] == 10 and t["ret"] == void and t["args"] == [i32])
    ft_item = c.ntypes  # (bytes, i32) -> void, appended
    nat_event = c.nfunctions + c.nnatives
    nat_item = nat_event + 1
    assert nat_event == max(max(f.findex for f in c.funcs), max(n[3] for n in c.natives)) + 1
    s_lib = c.nstrings
    int_base = c.nints

    IDS = list(dict.fromkeys(ev for ev, _ in EVENTS))  # one id per event name
    assert len(IDS) > 4
    hooks = {}  # findex -> (extra reg types, op bytes, op count)

    def fn(name):
        if name not in byname:
            sys.exit(f"function not found: {name}")
        f = fun_by_findex[byname[name]]
        assert f.findex not in hooks, name
        return f

    for ev, fname in EVENTS:
        f = fn(fname); r = len(f.regs)
        ops = [bytes([OP["Int"]]) + E(r) + E(int_base + IDS.index(ev)),
               bytes([OP["Call1"]]) + E(r + 1) + E(nat_event) + E(r)]
        hooks[f.findex] = ([i32, void], ops, IDS.index(ev))

    for kind, fname, arg in ITEM_HOOKS:
        f = fn(fname); b = len(f.regs)
        rItem, rKind, rIdx, rC, rStr, rBytes, rStrike, rV = range(b, b + 8)
        regs = [t_item, t_kind, i32, i32, t_str, tbytes, i32, void]
        ops = []  # (opcode, args) with 'END' placeholders for jumps
        if kind == "weapon":
            ops += [("JNull", [arg, "END"]), ("Field", [rItem, arg, f_item]), ("Field", [rStrike, arg, f_cycle])]
        else:
            ops += [("Mov", [rItem, arg]), ("Int", [rStrike, int_base + 0])]
        ops += [("JNull", [rItem, "END"]), ("Field", [rKind, rItem, f_kind]), ("JNull", [rKind, "END"]),
                ("EnumIndex", [rIdx, rKind]), ("Int", [rC, int_base + 4]),  # ctor 4 = __D_Passive has no id
                ("JEq", [rIdx, rC, "END"]), ("EnumField", [rStr, rKind, 0, 0]), ("JNull", [rStr, "END"]),
                ("Field", [rBytes, rStr, f_bytes]), ("Call2", [rV, nat_item, rBytes, rStrike])]
        enc = [bytes([OP[o]]) + b"".join(E(len(ops) - (i + 1) if a == "END" else a) for a in args)
               for i, (o, args) in enumerate(ops)]
        hooks[f.findex] = (regs, enc, None)

    # warn about double fire through super calls
    for fi, (_, _, eid) in hooks.items():
        for op, args in fun_by_findex[fi].ops:
            if eid is not None and op in hlbc.CALLS and args[1] in hooks and args[1] != fi and hooks[args[1]][2] is not None:
                print(f"note: {IDS[eid]} calls {IDS[hooks[args[1]][2]]}")

    out = bytearray(data[:4])
    out += E(c.flags)
    for v in (c.nints + len(IDS), c.nfloats, c.nstrings + 3, c.ntypes + 1, c.nglobals,
              c.nnatives + 2, c.nfunctions, c.nconstants, c.entry):
        out += E(v)
    r = hlbc.R(data); r.p = 4; r.idx()
    for _ in range(9): r.idx()
    out += data[r.p:c.floats_pos]
    out += b"".join(i.to_bytes(4, "little", signed=True) for i in range(len(IDS)))
    out += data[c.floats_pos:c.strings_pos]
    # strings: [i32 size][data][lens]
    size = int.from_bytes(data[c.strings_pos:c.strings_pos + 4], "little")
    dstart = c.strings_pos + 4
    new_strings = [NATIVE_LIB, "dch_event", "dch_item"]
    extra = b"".join(x.encode() + b"\0" for x in new_strings)
    out += (size + len(extra)).to_bytes(4, "little")
    out += data[dstart:dstart + size] + extra
    out += data[dstart + size:c.strings_end] + b"".join(E(len(x)) for x in new_strings)
    out += data[c.strings_end:c.globals_pos] + bytes([10, 2]) + E(tbytes) + E(i32) + E(void)
    out += data[c.globals_pos:c.funcs_pos]
    out += E(s_lib) + E(s_lib + 1) + E(ft_event) + E(nat_event)
    out += E(s_lib) + E(s_lib + 2) + E(ft_item) + E(nat_item)

    for f in c.funcs:
        h = hooks.get(f.findex)
        if h is None:
            out += data[f.start:f.end]
            continue
        regs, ops, _ = h
        m = len(ops)
        assert m <= 15
        out += E(f.type) + E(f.findex) + E(len(f.regs) + len(regs)) + E(len(f.ops) + m)
        out += b"".join(E(t) for t in f.regs + regs)
        out += b"".join(ops)
        out += data[f.ops_pos:f.ops_end]
        # debug: reuse the first file marker, then m ops on line 0 (delta 0, count m)
        assert data[f.ops_end] & 1, "debug stream must start with a file marker"
        out += data[f.ops_end:f.ops_end + 2] + bytes([(m << 2) | 2])
        out += data[f.ops_end:f.debug_end]
        out += E(len(f.assigns))
        for name, pos in f.assigns:
            out += E(name) + E(pos + m if pos >= 0 else pos)
    out += data[c.consts_pos:]

    check = hlbc.Code(bytes(out))  # must parse back
    assert check.nnatives == c.nnatives + 2 and check.strings[s_lib + 2] == "dch_item"
    assert check.types[ft_item] == {"kind": 10, "args": [tbytes, i32], "ret": void}
    open(dst, "wb").write(out)
    with open(os.path.join(HERE, "events.h"), "w") as h:
        h.write("// generated by patch.py\n")
        for eid, ev in enumerate(IDS):
            h.write(f"#define EV_{ev} {eid}\n")
        h.write(f"#define EV_COUNT {len(IDS)}\n")
        h.write("static const char *EV_NAMES[] = {" + ", ".join(f'"{e}"' for e in IDS) + "};\n")
    print(f"patched {len(hooks)} functions -> {os.path.normpath(dst)}")


if __name__ == "__main__":
    main()
