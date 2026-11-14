// dchpatch: inserts haptics calls into Dead Cells' HashLink bytecode (hlboot.dat, HLB v4).
//   dchpatch status <hlboot.dat>            prints 1 if already patched by this mod, 0 if original
//   dchpatch patch <original> <output>      writes the patched game file
// Each hooked function gets a call to the native haptics.dch_event(id) (or dch_item(id, strike) for
// weapons/skills) inserted before its first op. Untouched sections are copied byte for byte.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *name, *fn; } EVENTS[] = {
#define EV(n, f) { #n, f },
#include "events.def"
#undef EV
};
#define NEVENTS (int)(sizeof EVENTS / sizeof *EVENTS)

// hooks passing the item id (InventItemKind's String) + combo strike to haptics.dch_item(id, strike)
static const struct { int weapon; const char *fn; int arg; } ITEM_HOOKS[] = {
    { 1, "tool.hero.weap.BeheadedWeaponsManager.onWeaponExecute", 1 },          // arg 1 = tool.Weapon
    { 0, "tool.hero.activeSkills.BeheadedActiveSkillsManager.useSkillItem", 2 }, // arg 2 = InventItem
};

// opcode arg counts from hl/src/opcodes.h (-1 = variable)
static const signed char NARGS[] = {
    2, 2, 2, 2, 2, 2, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 1, 1, 2, 3, 4, 5, 6, -1, -1, -1, -1,
    2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 2, 2, 2, 2, 2, 2, 2, 0,
    1, 1, 1, -1, 1, 2, 1, 3, 3, 3, 3, 3, 3, 3, 3, 1, 2, 2, 2, 2, 2, 2, 2, -1, 2, 2, 4, 3, 0, 2, 3, 0, 3, 3,
};
enum { OMov = 0, OInt = 1, OCall1 = 25, OCall2 = 26, OField = 38, OJNull = 46, OJEq = 56, OSwitch = 70,
       OEnumIndex = 92, OEnumField = 93 };

static void die(const char *m, const char *a) { fprintf(stderr, "dchpatch: %s%s\n", m, a ? a : ""); exit(1); }

// ---------- reader ----------
typedef struct { const uint8_t *b; size_t n, p; } R;
static int u8(R *r) { if (r->p >= r->n) die("truncated file", 0); return r->b[r->p++]; }
static int idx(R *r) {
    int c = u8(r);
    if (!(c & 0x80)) return c & 0x7F;
    int v;
    if (!(c & 0x40)) v = u8(r) | ((c & 31) << 8);
    else { int b1 = u8(r), b2 = u8(r), b3 = u8(r); v = ((c & 31) << 24) | (b1 << 16) | (b2 << 8) | b3; }
    return c & 0x20 ? -v : v;
}
static int32_t i32(R *r) { if (r->p + 4 > r->n) die("truncated file", 0); int32_t v; memcpy(&v, r->b + r->p, 4); r->p += 4; return v; }

typedef struct { int name, type; } Field;
typedef struct { int kind, name, super, nfields, nprotos, nbind, nargs, ret; Field *fields; int *protos, *binds, *args; } Type;
typedef struct { int type, findex, nregs, nops; size_t start, regs_pos, ops_pos, ops_end, debug_end, end; int nassign; int *assigns; } Func;

typedef struct {
    const uint8_t *d; size_t n;
    int flags, nints, nfloats, nstrings, ntypes, nglobals, nnatives, nfunctions, nconstants, entry;
    size_t header_end, floats_pos, strings_pos, strings_end, globals_pos, natives_pos, funcs_pos, consts_pos;
    const char **str; int *slen;
    Type *types; int *nat_findex; Func *funcs;
} Code;

static void strings(R *r, int count, const char ***out, int **lens) {
    int size = i32(r); size_t off = r->p; r->p += size;
    if (r->p > r->n) die("bad string block", 0);
    *out = malloc(sizeof(char *) * (count + 1)); *lens = malloc(sizeof(int) * (count + 1));
    for (int i = 0; i < count; i++) { int l = idx(r); (*out)[i] = (const char *)r->b + off; (*lens)[i] = l; off += l + 1; }
}

static void parse(Code *c, const uint8_t *d, size_t n) {
    memset(c, 0, sizeof *c);
    c->d = d; c->n = n;
    if (n < 4 || memcmp(d, "HLB", 3)) die("not a HashLink bytecode file", 0);
    if (d[3] != 4) die("unsupported bytecode version (game updated?)", 0);
    R r = { d, n, 4 };
    c->flags = idx(&r);
    int *h[] = { &c->nints, &c->nfloats, &c->nstrings, &c->ntypes, &c->nglobals, &c->nnatives, &c->nfunctions, &c->nconstants, &c->entry };
    for (int i = 0; i < 9; i++) *h[i] = idx(&r);
    c->header_end = r.p;
    r.p += 4 * (size_t)c->nints;
    c->floats_pos = r.p; r.p += 8 * (size_t)c->nfloats;
    c->strings_pos = r.p;
    strings(&r, c->nstrings, &c->str, &c->slen);
    c->strings_end = r.p;
    if (c->flags & 1) { const char **ds; int *dl; int nd = idx(&r); strings(&r, nd, &ds, &dl); free(ds); free(dl); }
    c->types = calloc(c->ntypes + 1, sizeof(Type));
    for (int t = 0; t < c->ntypes; t++) {
        Type *ty = &c->types[t];
        ty->kind = u8(&r); ty->name = -1; ty->super = -1;
        if (ty->kind == 10 || ty->kind == 20) {
            ty->nargs = u8(&r); ty->args = malloc(sizeof(int) * (ty->nargs + 1));
            for (int i = 0; i < ty->nargs; i++) ty->args[i] = idx(&r);
            ty->ret = idx(&r);
        } else if (ty->kind == 11 || ty->kind == 21) {
            ty->name = idx(&r); ty->super = idx(&r); idx(&r);
            ty->nfields = idx(&r); ty->nprotos = idx(&r); ty->nbind = idx(&r);
            ty->fields = malloc(sizeof(Field) * (ty->nfields + 1));
            for (int i = 0; i < ty->nfields; i++) { ty->fields[i].name = idx(&r); ty->fields[i].type = idx(&r); }
            ty->protos = malloc(sizeof(int) * 2 * (ty->nprotos + 1));
            for (int i = 0; i < ty->nprotos; i++) { ty->protos[2 * i] = idx(&r); ty->protos[2 * i + 1] = idx(&r); idx(&r); }
            ty->binds = malloc(sizeof(int) * 2 * (ty->nbind + 1));
            for (int i = 0; i < ty->nbind; i++) { ty->binds[2 * i] = idx(&r); ty->binds[2 * i + 1] = idx(&r); }
        } else if (ty->kind == 14 || ty->kind == 19 || ty->kind == 22) idx(&r);
        else if (ty->kind == 15) { int nf = idx(&r); for (int i = 0; i < nf; i++) { idx(&r); idx(&r); } }
        else if (ty->kind == 17) ty->name = idx(&r);
        else if (ty->kind == 18) {
            ty->name = idx(&r); idx(&r);
            int nc = idx(&r);
            for (int i = 0; i < nc; i++) { idx(&r); int np = idx(&r); for (int j = 0; j < np; j++) idx(&r); }
        } else if (ty->kind >= 23) die("unknown type kind", 0);
    }
    c->globals_pos = r.p;
    for (int i = 0; i < c->nglobals; i++) idx(&r);
    c->natives_pos = r.p;
    c->nat_findex = malloc(sizeof(int) * (c->nnatives + 1));
    for (int i = 0; i < c->nnatives; i++) { idx(&r); idx(&r); idx(&r); c->nat_findex[i] = idx(&r); }
    c->funcs_pos = r.p;
    c->funcs = calloc(c->nfunctions + 1, sizeof(Func));
    for (int k = 0; k < c->nfunctions; k++) {
        Func *f = &c->funcs[k];
        f->start = r.p;
        f->type = idx(&r); f->findex = idx(&r); f->nregs = idx(&r); f->nops = idx(&r);
        f->regs_pos = r.p;
        for (int i = 0; i < f->nregs; i++) idx(&r);
        f->ops_pos = r.p;
        for (int i = 0; i < f->nops; i++) {
            int op = u8(&r);
            if (op >= (int)sizeof NARGS) die("unknown opcode", 0);
            int na = NARGS[op];
            if (na >= 0) for (int j = 0; j < na; j++) idx(&r);
            else if (op == OSwitch) { idx(&r); int cnt = idx(&r); for (int j = 0; j < cnt; j++) idx(&r); idx(&r); }
            else { idx(&r); idx(&r); int cnt = u8(&r); for (int j = 0; j < cnt; j++) idx(&r); }
        }
        f->ops_end = r.p;
        if (c->flags & 1) {
            for (int i = 0; i < f->nops;) {
                int ch = u8(&r);
                if (ch & 1) u8(&r);
                else if (ch & 2) i += (ch >> 2) & 15;
                else if (ch & 4) i++;
                else { r.p += 2; i++; }
            }
            f->debug_end = r.p;
            f->nassign = idx(&r);
            f->assigns = malloc(sizeof(int) * 2 * (f->nassign + 1));
            for (int i = 0; i < f->nassign; i++) { f->assigns[2 * i] = idx(&r); f->assigns[2 * i + 1] = idx(&r); }
        } else die("bytecode without debug info is not supported", 0);
        f->end = r.p;
    }
    c->consts_pos = r.p;
    for (int i = 0; i < c->nconstants; i++) { idx(&r); int nf = idx(&r); for (int j = 0; j < nf; j++) idx(&r); }
    if (r.p != n) die("unexpected data at end of file", 0);
}

static int str_is(const Code *c, int s, const char *v) { return s >= 0 && s < c->nstrings && (int)strlen(v) == c->slen[s] && !memcmp(c->str[s], v, c->slen[s]); }
static int has_string(const Code *c, const char *v) { for (int i = 0; i < c->nstrings; i++) if (str_is(c, i, v)) return 1; return 0; }

// field list of an object type including its superclasses (HashLink field indexes count from the root)
static int all_fields(const Code *c, int t, Field *out, int max) {
    int chain[64], n = 0, cnt = 0;
    for (int x = t; x >= 0 && n < 64; x = c->types[x].super) chain[n++] = x;
    for (int i = n - 1; i >= 0; i--)
        for (int j = 0; j < c->types[chain[i]].nfields && cnt < max; j++) out[cnt++] = c->types[chain[i]].fields[j];
    return cnt;
}

static int field_index(const Code *c, int t, const char *name) {
    static Field f[4096];
    int n = all_fields(c, t, f, 4096);
    for (int i = 0; i < n; i++) if (str_is(c, f[i].name, name)) return i;
    die("field not found: ", name);
    return -1;
}

static int type_named(const Code *c, int kind, const char *name) {
    for (int i = 0; i < c->ntypes; i++) if (c->types[i].kind == kind && str_is(c, c->types[i].name, name)) return i;
    die("type not found: ", name);
    return -1;
}

static int type_kind(const Code *c, int kind) {
    for (int i = 0; i < c->ntypes; i++) if (c->types[i].kind == kind) return i;
    die("basic type missing", 0);
    return -1;
}

// "Class.method" name of each function findex, as HashLink's own tools derive it: prototypes give
// instance methods, bindings on the "$Class" object give static methods. Later types override earlier ones;
// lookups take the first findex (in first-assignment order) whose final name matches.
typedef struct { int type, slot, is_proto; } NameRef;
static NameRef *names; static int *order; static int norder;

static void build_names(const Code *c) {
    int total = c->nfunctions + c->nnatives;
    names = calloc(total + 1, sizeof(NameRef));
    order = malloc(sizeof(int) * (total + 1));
    for (int i = 0; i < total; i++) names[i].type = -1;
    for (int t = 0; t < c->ntypes; t++) {
        const Type *ty = &c->types[t];
        if (ty->kind != 11) continue;
        for (int i = 0; i < ty->nprotos; i++) {
            int fi = ty->protos[2 * i + 1];
            if (fi < 0 || fi >= total) continue;
            if (names[fi].type < 0) order[norder++] = fi;
            names[fi] = (NameRef){ t, i, 1 };
        }
        for (int i = 0; i < ty->nbind; i++) {
            int fi = ty->binds[2 * i + 1];
            if (fi < 0 || fi >= total) continue;
            if (names[fi].type < 0) order[norder++] = fi;
            names[fi] = (NameRef){ t, i, 0 };
        }
    }
}

static int name_matches(const Code *c, int fi, const char *want) {
    NameRef n = names[fi];
    const Type *ty = &c->types[n.type];
    const char *cls = c->str[ty->name]; int clen = c->slen[ty->name];
    int member;
    if (n.is_proto) member = ty->protos[2 * n.slot];
    else {
        static Field f[4096];
        int nf = all_fields(c, n.type, f, 4096), fid = ty->binds[2 * n.slot];
        if (fid < 0 || fid >= nf) return 0;
        member = f[fid].name;
        while (clen > 0 && *cls == '$') { cls++; clen--; } // static methods live on "$Class"
    }
    size_t wl = strlen(want);
    return wl == (size_t)clen + 1 + c->slen[member] && !memcmp(want, cls, clen) && want[clen] == '.'
        && !memcmp(want + clen + 1, c->str[member], c->slen[member]);
}

static Func *find_fn(const Code *c, const char *name) {
    int fi = -1;
    for (int i = 0; i < norder && fi < 0; i++) if (name_matches(c, order[i], name)) fi = order[i];
    if (fi < 0) die("function not found (unsupported game version?): ", name);
    for (int k = 0; k < c->nfunctions; k++) if (c->funcs[k].findex == fi) return &c->funcs[k];
    die("function body not found: ", name);
    return NULL;
}

// ---------- writer ----------
typedef struct { uint8_t *b; size_t n, cap; } W;
static void put(W *w, const void *p, size_t n) {
    if (w->n + n > w->cap) { w->cap = (w->n + n) * 2; w->b = realloc(w->b, w->cap); if (!w->b) die("out of memory", 0); }
    memcpy(w->b + w->n, p, n); w->n += n;
}
static void put8(W *w, int v) { uint8_t b = (uint8_t)v; put(w, &b, 1); }
static void put32(W *w, int32_t v) { put(w, &v, 4); }
static void E(W *w, int v) {
    int neg = v < 0; unsigned u = neg ? -v : v;
    if (!neg && u < 0x80) put8(w, u);
    else if (u < 0x2000) { put8(w, 0x80 | (neg ? 0x20 : 0) | (u >> 8)); put8(w, u & 0xFF); }
    else { put8(w, 0xC0 | (neg ? 0x20 : 0) | (u >> 24)); put8(w, (u >> 16) & 0xFF); put8(w, (u >> 8) & 0xFF); put8(w, u & 0xFF); }
}

typedef struct { int nregs, regs[8]; W ops; int nops; } Hook;

static uint8_t *readfile(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open ", path);
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n ? *n : 1);
    if (fread(b, 1, *n, f) != *n) die("cannot read ", path);
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "status")) {
        size_t n; uint8_t *d = readfile(argv[2], &n);
        Code c; parse(&c, d, n);
        printf("%d\n", has_string(&c, "dch_event"));
        return 0;
    }
    if (argc != 4 || strcmp(argv[1], "patch")) {
        fprintf(stderr, "usage: dchpatch status <hlboot.dat>\n       dchpatch patch <original> <output>\n");
        return 2;
    }
    size_t n; uint8_t *d = readfile(argv[2], &n);
    Code c; parse(&c, d, n);
    if (has_string(&c, "dch_event")) die("the source file is already patched; pass the original", 0);
    build_names(&c);

    int i32t = type_kind(&c, 3), voidt = type_kind(&c, 0), bytest = type_kind(&c, 8);
    int t_weapon = type_named(&c, 11, "tool.Weapon"), t_item = type_named(&c, 11, "tool.InventItem");
    int t_str = type_named(&c, 11, "String"), t_kind = type_named(&c, 18, "tool.InventItemKind");
    int f_item = field_index(&c, t_weapon, "item"), f_cycle = field_index(&c, t_weapon, "_cycle");
    int f_kind = field_index(&c, t_item, "kind"), f_bytes = field_index(&c, t_str, "bytes");
    int ft_event = -1;
    for (int i = 0; i < c.ntypes && ft_event < 0; i++) {
        const Type *t = &c.types[i];
        if (t->kind == 10 && t->ret == voidt && t->nargs == 1 && t->args[0] == i32t) ft_event = i;
    }
    if (ft_event < 0) die("function type (i32)->void missing", 0);
    int ft_item = c.ntypes; // (bytes, i32) -> void, appended
    int nat_event = c.nfunctions + c.nnatives, nat_item = nat_event + 1;
    int maxf = -1;
    for (int k = 0; k < c.nfunctions; k++) if (c.funcs[k].findex > maxf) maxf = c.funcs[k].findex;
    for (int k = 0; k < c.nnatives; k++) if (c.nat_findex[k] > maxf) maxf = c.nat_findex[k];
    if (nat_event != maxf + 1) die("unexpected function index layout", 0);
    int s_lib = c.nstrings, int_base = c.nints;

    // one hook per function, keyed by position in c.funcs
    Hook **hooks = calloc(c.nfunctions + 1, sizeof(Hook *));
    for (int e = 0; e < NEVENTS; e++) {
        Func *f = find_fn(&c, EVENTS[e].fn);
        int k = (int)(f - c.funcs);
        if (hooks[k]) die("function hooked twice: ", EVENTS[e].fn);
        Hook *h = hooks[k] = calloc(1, sizeof(Hook));
        int r = f->nregs;
        h->nregs = 2; h->regs[0] = i32t; h->regs[1] = voidt;
        put8(&h->ops, OInt); E(&h->ops, r); E(&h->ops, int_base + e);
        put8(&h->ops, OCall1); E(&h->ops, r + 1); E(&h->ops, nat_event); E(&h->ops, r);
        h->nops = 2;
    }
    for (int x = 0; x < (int)(sizeof ITEM_HOOKS / sizeof *ITEM_HOOKS); x++) {
        Func *f = find_fn(&c, ITEM_HOOKS[x].fn);
        int k = (int)(f - c.funcs), b = f->nregs, arg = ITEM_HOOKS[x].arg;
        if (hooks[k]) die("function hooked twice: ", ITEM_HOOKS[x].fn);
        Hook *h = hooks[k] = calloc(1, sizeof(Hook));
        int rItem = b, rKind = b + 1, rIdx = b + 2, rC = b + 3, rStr = b + 4, rBytes = b + 5, rStrike = b + 6, rV = b + 7;
        int regs[8] = { t_item, t_kind, i32t, i32t, t_str, bytest, i32t, voidt };
        h->nregs = 8; memcpy(h->regs, regs, sizeof regs);
        // ops as {opcode, args...}; END marks "jump to first original op" (offset relative to the next op)
        enum { END = 0x7fffffff };
        int ops[16][6]; int no = 0; // [0] opcode, [1..4] args, [5] arg count
#define OP(...) do { int t_[] = { __VA_ARGS__ }; memset(ops[no], -1, sizeof ops[no]); memcpy(ops[no], t_, sizeof t_); ops[no][5] = (int)(sizeof t_ / sizeof(int)) - 1; no++; } while (0)
        if (ITEM_HOOKS[x].weapon) { OP(OJNull, arg, END); OP(OField, rItem, arg, f_item); OP(OField, rStrike, arg, f_cycle); }
        else { OP(OMov, rItem, arg); OP(OInt, rStrike, int_base + 0); }
        OP(OJNull, rItem, END); OP(OField, rKind, rItem, f_kind); OP(OJNull, rKind, END);
        OP(OEnumIndex, rIdx, rKind); OP(OInt, rC, int_base + 4); // constructor 4 = __D_Passive carries no id
        OP(OJEq, rIdx, rC, END); OP(OEnumField, rStr, rKind, 0, 0); OP(OJNull, rStr, END);
        OP(OField, rBytes, rStr, f_bytes); OP(OCall2, rV, nat_item, rBytes, rStrike);
#undef OP
        for (int i = 0; i < no; i++) {
            put8(&h->ops, ops[i][0]);
            for (int a = 1; a <= ops[i][5]; a++) E(&h->ops, ops[i][a] == END ? no - (i + 1) : ops[i][a]);
        }
        h->nops = no;
    }

    W w = {0};
    put(&w, d, 4);
    E(&w, c.flags);
    int hdr[] = { c.nints + NEVENTS, c.nfloats, c.nstrings + 3, c.ntypes + 1, c.nglobals, c.nnatives + 2, c.nfunctions, c.nconstants, c.entry };
    for (int i = 0; i < 9; i++) E(&w, hdr[i]);
    put(&w, d + c.header_end, c.floats_pos - c.header_end); // original ints, then one per event id
    for (int i = 0; i < NEVENTS; i++) put32(&w, i);
    put(&w, d + c.floats_pos, c.strings_pos - c.floats_pos);
    const char *ns[] = { "haptics", "dch_event", "dch_item" };
    int32_t size; memcpy(&size, d + c.strings_pos, 4);
    size_t dstart = c.strings_pos + 4, extra = 0;
    for (int i = 0; i < 3; i++) extra += strlen(ns[i]) + 1;
    put32(&w, size + (int32_t)extra);
    put(&w, d + dstart, size);
    for (int i = 0; i < 3; i++) put(&w, ns[i], strlen(ns[i]) + 1);
    put(&w, d + dstart + size, c.strings_end - (dstart + size));
    for (int i = 0; i < 3; i++) E(&w, (int)strlen(ns[i]));
    put(&w, d + c.strings_end, c.globals_pos - c.strings_end);
    put8(&w, 10); put8(&w, 2); E(&w, bytest); E(&w, i32t); E(&w, voidt);
    put(&w, d + c.globals_pos, c.funcs_pos - c.globals_pos);
    E(&w, s_lib); E(&w, s_lib + 1); E(&w, ft_event); E(&w, nat_event);
    E(&w, s_lib); E(&w, s_lib + 2); E(&w, ft_item); E(&w, nat_item);
    int patched = 0;
    for (int k = 0; k < c.nfunctions; k++) {
        const Func *f = &c.funcs[k];
        const Hook *h = hooks[k];
        if (!h) { put(&w, d + f->start, f->end - f->start); continue; }
        int m = h->nops;
        if (m > 15) die("hook too long", 0);
        E(&w, f->type); E(&w, f->findex); E(&w, f->nregs + h->nregs); E(&w, f->nops + m);
        put(&w, d + f->regs_pos, f->ops_pos - f->regs_pos);
        for (int i = 0; i < h->nregs; i++) E(&w, h->regs[i]);
        put(&w, h->ops.b, h->ops.n);
        put(&w, d + f->ops_pos, f->ops_end - f->ops_pos);
        // debug lines: repeat the first file marker, then m inserted ops on line 0 (delta 0, count m)
        if (!(d[f->ops_end] & 1)) die("debug stream must start with a file marker", 0);
        put(&w, d + f->ops_end, 2); put8(&w, (m << 2) | 2);
        put(&w, d + f->ops_end, f->debug_end - f->ops_end);
        E(&w, f->nassign);
        for (int i = 0; i < f->nassign; i++) { int pos = f->assigns[2 * i + 1]; E(&w, f->assigns[2 * i]); E(&w, pos >= 0 ? pos + m : pos); }
        patched++;
    }
    put(&w, d + c.consts_pos, c.n - c.consts_pos);

    Code check; parse(&check, w.b, w.n); // self-check: the output must parse back cleanly
    if (check.nnatives != c.nnatives + 2 || !str_is(&check, s_lib + 2, "dch_item")) die("self-check failed", 0);
    FILE *o = fopen(argv[3], "wb");
    if (!o || fwrite(w.b, 1, w.n, o) != w.n || fclose(o)) die("cannot write ", argv[3]);
    printf("patched %d functions\n", patched);
    return 0;
}
