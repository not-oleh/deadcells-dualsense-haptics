"""Minimal HashLink bytecode (HLB v4) reader/patcher. Unmodified sections are copied as raw bytes."""
import struct

# arg counts from hl/src/opcodes.h (-1 = variable)
OPS = [
    ("Mov", 2), ("Int", 2), ("Float", 2), ("Bool", 2), ("Bytes", 2), ("String", 2), ("Null", 1),
    ("Add", 3), ("Sub", 3), ("Mul", 3), ("SDiv", 3), ("UDiv", 3), ("SMod", 3), ("UMod", 3),
    ("Shl", 3), ("SShr", 3), ("UShr", 3), ("And", 3), ("Or", 3), ("Xor", 3), ("Neg", 2), ("Not", 2),
    ("Incr", 1), ("Decr", 1), ("Call0", 2), ("Call1", 3), ("Call2", 4), ("Call3", 5), ("Call4", 6),
    ("CallN", -1), ("CallMethod", -1), ("CallThis", -1), ("CallClosure", -1),
    ("StaticClosure", 2), ("InstanceClosure", 3), ("VirtualClosure", 3),
    ("GetGlobal", 2), ("SetGlobal", 2), ("Field", 3), ("SetField", 3), ("GetThis", 2), ("SetThis", 2),
    ("DynGet", 3), ("DynSet", 3), ("JTrue", 2), ("JFalse", 2), ("JNull", 2), ("JNotNull", 2),
    ("JSLt", 3), ("JSGte", 3), ("JSGt", 3), ("JSLte", 3), ("JULt", 3), ("JUGte", 3),
    ("JNotLt", 3), ("JNotGte", 3), ("JEq", 3), ("JNotEq", 3), ("JAlways", 1),
    ("ToDyn", 2), ("ToSFloat", 2), ("ToUFloat", 2), ("ToInt", 2), ("SafeCast", 2), ("UnsafeCast", 2),
    ("ToVirtual", 2), ("Label", 0), ("Ret", 1), ("Throw", 1), ("Rethrow", 1), ("Switch", -1),
    ("NullCheck", 1), ("Trap", 2), ("EndTrap", 1), ("GetI8", 3), ("GetI16", 3), ("GetMem", 3),
    ("GetArray", 3), ("SetI8", 3), ("SetI16", 3), ("SetMem", 3), ("SetArray", 3), ("New", 1),
    ("ArraySize", 2), ("Type", 2), ("GetType", 2), ("GetTID", 2), ("Ref", 2), ("Unref", 2),
    ("Setref", 2), ("MakeEnum", -1), ("EnumAlloc", 2), ("EnumIndex", 2), ("EnumField", 4),
    ("SetEnumField", 3), ("Assert", 0), ("RefData", 2), ("RefOffset", 3), ("Nop", 0),
    ("Prefetch", 3), ("Asm", 3),
]
OP = {n: i for i, (n, _) in enumerate(OPS)}
CALLS = {OP["Call0"], OP["Call1"], OP["Call2"], OP["Call3"], OP["Call4"], OP["CallN"]}


def enc_index(v):
    neg = v < 0
    v = -v if neg else v
    if not neg and v < 0x80:
        return bytes([v])
    if v < 0x2000:
        return bytes([0x80 | (0x20 if neg else 0) | (v >> 8), v & 0xFF])
    assert v < 0x20000000
    return bytes([0xC0 | (0x20 if neg else 0) | (v >> 24), (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF])


class R:
    def __init__(s, b, p=0):
        s.b, s.p = b, p

    def byte(s):
        v = s.b[s.p]; s.p += 1; return v

    def idx(s):
        b = s.b; p = s.p; c = b[p]
        if not c & 0x80:
            s.p = p + 1; return c & 0x7F
        if not c & 0x40:
            v = b[p + 1] | ((c & 31) << 8); s.p = p + 2
        else:
            v = ((c & 31) << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]; s.p = p + 4
        return -v if c & 0x20 else v

    def i32(s):
        v = struct.unpack_from("<i", s.b, s.p)[0]; s.p += 4; return v


class Func:
    pass


class Code:
    def __init__(s, data):
        s.data = data
        r = R(data)
        assert data[:3] == b"HLB"
        s.version = data[3]; r.p = 4
        assert s.version == 4, s.version
        s.flags = r.idx()
        (s.nints, s.nfloats, s.nstrings, s.ntypes, s.nglobals, s.nnatives, s.nfunctions,
         s.nconstants, s.entry) = [r.idx() for _ in range(9)]
        s.ints = [r.i32() for _ in range(s.nints)]
        s.floats_pos = r.p; r.p += 8 * s.nfloats
        s.strings_pos = r.p
        s.strings = s._strings(r, s.nstrings)
        s.strings_end = r.p
        if s.flags & 1:
            s.ndebug = r.idx()
            s.debugfiles = s._strings(r, s.ndebug)
        s.types_pos = r.p
        s.types = [s._type(r) for _ in range(s.ntypes)]
        s.globals_pos = r.p
        s.globals = [r.idx() for _ in range(s.nglobals)]
        s.natives_pos = r.p
        s.natives = []
        for _ in range(s.nnatives):
            s.natives.append((r.idx(), r.idx(), r.idx(), r.idx()))  # lib, name, type, findex
        s.funcs_pos = r.p
        s.funcs = [s._func(r) for _ in range(s.nfunctions)]
        s.consts_pos = r.p
        for _ in range(s.nconstants):
            r.idx(); n = r.idx()
            for _ in range(n): r.idx()
        assert r.p == len(data), (r.p, len(data))

    def _strings(s, r, n):
        size = r.i32(); start = r.p; r.p += size
        out = []; off = start
        for _ in range(n):
            l = r.idx()
            out.append(s.data[off:off + l].decode("utf8", "replace")); off += l + 1
        return out

    def _type(s, r):
        k = r.byte(); t = {"kind": k}
        if k in (10, 20):
            t["args"] = [r.idx() for _ in range(r.byte())]; t["ret"] = r.idx()
        elif k in (11, 21):
            t["name"] = s.strings[r.idx()]; t["super"] = r.idx(); t["global"] = r.idx()
            nf, np_, nb = r.idx(), r.idx(), r.idx()
            t["fields"] = [(s.strings[r.idx()], r.idx()) for _ in range(nf)]
            t["protos"] = [(s.strings[r.idx()], r.idx(), r.idx()) for _ in range(np_)]
            t["bindings"] = [(r.idx(), r.idx()) for _ in range(nb)]
        elif k in (14, 19, 22):
            t["param"] = r.idx()
        elif k == 15:
            t["fields"] = [(s.strings[r.idx()], r.idx()) for _ in range(r.idx())]
        elif k == 17:
            t["name"] = s.strings[r.idx()]
        elif k == 18:
            t["name"] = s.strings[r.idx()]; r.idx()
            for _ in range(r.idx()):
                r.idx()
                for _ in range(r.idx()): r.idx()
        else:
            assert k < 23, k
        return t

    def _func(s, r):
        f = Func(); f.start = r.p
        f.type = r.idx(); f.findex = r.idx(); nregs = r.idx(); nops = r.idx()
        f.regs = [r.idx() for _ in range(nregs)]
        f.ops_pos = r.p
        f.ops = []
        for _ in range(nops):
            op = r.byte(); n = OPS[op][1]
            if n >= 0:
                args = [r.idx() for _ in range(n)]
            elif op == OP["Switch"]:
                a, c = r.idx(), r.idx()
                args = [a, c] + [r.idx() for _ in range(c)] + [r.idx()]
            else:
                a, b, c = r.idx(), r.idx(), r.byte()
                args = [a, b, c] + [r.idx() for _ in range(c)]
            f.ops.append((op, args))
        f.ops_end = r.p
        if s.flags & 1:
            i = 0
            while i < nops:
                c = r.byte()
                if c & 1: r.byte()
                elif c & 2: i += (c >> 2) & 15
                elif c & 4: i += 1
                else: r.p += 2; i += 1
            f.debug_end = r.p
            f.assigns = [(r.idx(), r.idx()) for _ in range(r.idx())]
        f.end = r.p
        return f

    def names(s):
        """findex -> 'Class.method'"""
        out = {}
        for t in s.types:
            if t["kind"] != 11: continue
            cname = t["name"]
            for name, fi, _ in t["protos"]:
                out[fi] = cname + "." + name
            if t["bindings"]:
                allf = s._all_fields(t)
                for fid, fi in t["bindings"]:
                    out[fi] = cname.lstrip("$") + "." + allf[fid][0]
        return out

    def _all_fields(s, t):
        chain = []
        while True:
            chain.append(t)
            if t["super"] < 0: break
            t = s.types[t["super"]]
        fields = []
        for tt in reversed(chain): fields += tt["fields"]
        return fields
