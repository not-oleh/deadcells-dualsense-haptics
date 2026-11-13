// haptics.hdll — HashLink native called by the patched hlboot.dat: haptics.dch_event(id).
// Plays rumble patterns on the controller through the game's own SDL2 (works over USB and Bluetooth).
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include "events.h"
#include "hd.h"
#include "ch.h"

typedef struct _SDL_GameController SDL_GameController;
extern int SDL_NumJoysticks(void);
extern int SDL_IsGameController(int);
extern SDL_GameController *SDL_GameControllerOpen(int);
extern int SDL_GameControllerGetAttached(SDL_GameController *);
extern int SDL_GameControllerGetType(SDL_GameController *);
extern const char *SDL_GameControllerName(SDL_GameController *);
extern int SDL_GameControllerRumble(SDL_GameController *, unsigned short, unsigned short, unsigned);
extern short SDL_GameControllerGetAxis(SDL_GameController *, int);
extern unsigned char SDL_GameControllerGetButton(SDL_GameController *, int);
#define SDL_CONTROLLER_TYPE_PS5 7
#define SDL_CONTROLLER_AXIS_LEFTX 0
#define SDL_CONTROLLER_BUTTON_DPAD_LEFT 13
#define SDL_CONTROLLER_BUTTON_DPAD_RIGHT 14

typedef struct { unsigned char lo, hi; unsigned short ms; } Step; // lo = heavy motor, hi = light motor
#define MAX_STEPS 16
typedef struct { int prio, n; Step s[MAX_STEPS]; } Pattern;
#define P(prio, ...) { prio, sizeof((Step[]){__VA_ARGS__}) / sizeof(Step), {__VA_ARGS__} }
#define S(lo, hi, ms) { lo, hi, ms }

static const Pattern PATTERNS[EV_COUNT] = {
    [EV_STEP]          = P(0, S(0, 45, 25)),
    [EV_JUMP]          = P(2, S(60, 120, 60)),
    [EV_AIR_JUMP]      = P(2, S(40, 160, 60)),
    [EV_JUMP_DOWN]     = P(1, S(0, 80, 40)),
    [EV_LAND]          = P(2, S(120, 40, 70)),
    [EV_LAND_HARD]     = P(5, S(255, 120, 180), S(80, 0, 120)),
    [EV_DODGE]         = P(3, S(90, 60, 60), S(30, 120, 90)),
    [EV_DODGE_RICHTER] = P(3, S(90, 60, 60), S(30, 120, 90)),
    [EV_PERFECT_DODGE] = P(6, S(0, 255, 50), S(0, 0, 40), S(0, 255, 50)),
    [EV_DIVE]          = P(3, S(0, 180, 100)),
    [EV_DIVE_LAND]     = P(6, S(255, 255, 120), S(120, 40, 120)),
    [EV_HIT_DEALT]     = P(2, S(150, 90, 60)),
    [EV_CRIT]          = P(5, S(255, 200, 90)),
    [EV_PARRY]         = P(7, S(255, 255, 60), S(0, 0, 30), S(180, 0, 90)),
    [EV_DAMAGED]       = P(7, S(255, 180, 120), S(120, 60, 120)),
    [EV_FATAL_FALL]    = P(7, S(255, 120, 250)),
    [EV_HERO_DIE]      = P(10, S(255, 255, 300), S(0, 0, 120), S(200, 100, 250), S(0, 0, 100), S(120, 40, 600)),
    [EV_KILL]          = P(4, S(220, 60, 90), S(60, 0, 70)),
    [EV_BOSS_KILL]     = P(9, S(255, 255, 400), S(200, 120, 300), S(150, 80, 300), S(100, 40, 400), S(60, 0, 500)),
    [EV_HEAL]          = P(4, S(60, 0, 150), S(0, 0, 80), S(90, 0, 150), S(0, 0, 80), S(120, 0, 200)),
    [EV_FLASK]         = P(4, S(60, 0, 150), S(0, 0, 80), S(90, 0, 150), S(0, 0, 80), S(120, 0, 200)),
    [EV_GOLD]          = P(1, S(0, 70, 25)),
    [EV_CELLS]         = P(1, S(30, 90, 35)),
    [EV_BUY]           = P(6, S(0, 200, 50), S(0, 0, 40), S(80, 220, 60), S(150, 0, 120)),
    [EV_ITEM_PICK]     = P(5, S(80, 180, 80), S(150, 80, 120)),
    [EV_BLUEPRINT]     = P(5, S(0, 200, 60), S(0, 0, 50), S(0, 200, 60)),
    [EV_BOSS_CELL]     = P(8, S(255, 200, 300), S(120, 80, 300)),
    [EV_CHEST]         = P(5, S(180, 60, 120), S(80, 120, 150)),
    [EV_CURSED_CHEST]  = P(7, S(255, 60, 200), S(0, 0, 80), S(255, 60, 300)),
    [EV_DOOR_KICK]     = P(5, S(230, 120, 100)),
    [EV_DOOR_BREAK]    = P(5, S(255, 160, 140)),
    [EV_BREAK_GROUND]  = P(5, S(200, 120, 140)),
    [EV_TELEPORT]      = P(5, S(0, 120, 120), S(80, 200, 150), S(0, 60, 120)),
    [EV_CLIFF_GRAB]    = P(2, S(60, 100, 50)),
    [EV_CLIMB]         = P(1, S(0, 80, 40)),
    [EV_WATER]         = P(2, S(80, 40, 150)),
    [EV_INTERACT]      = P(2, S(40, 100, 50)),
    [EV_CURSED]        = P(7, S(200, 0, 250), S(0, 0, 80), S(200, 0, 250)),
    [EV_LEVEL_ENTER]   = P(3, S(60, 60, 200)),
    [EV_WAKEUP]        = P(3, S(100, 40, 150), S(60, 20, 200)),
    [EV_BOW_CHARGE]    = P(2, S(0, 60, 80)),
    // King Scepter: rumble whirls grip to grip while spinning
    [EV_SCEPTER_SPIN]  = P(4, S(230, 60, 45), S(60, 230, 45), S(230, 60, 45), S(60, 230, 45),
                              S(200, 50, 45), S(50, 200, 45), S(160, 40, 45), S(40, 160, 45)),
    [EV_SCEPTER_STOP]  = P(4, S(220, 140, 70), S(90, 30, 110)),
};
// dodge sweeps across the grips: lo motor sits in the left grip, hi motor in the right
static Pattern DODGE_RIGHT, DODGE_LEFT; // built in build_dodge()

// Smooth roll wave: equal-power crossfade from the grip you leave to the grip you roll toward,
// 16 steps x 15 ms (~ roll length). Strength swells mid-roll so the wave feels like it carries through.
static void build_dodge(void) {
    const int steps = MAX_STEPS, ms = 19; // ~300 ms, the length that felt best in HD tests
    DODGE_RIGHT.prio = DODGE_LEFT.prio = 3;
    DODGE_RIGHT.n = DODGE_LEFT.n = steps;
    for (int i = 0; i < steps; i++) {
        double t = (double)i / (steps - 1);
        double amp = 200 + 55 * sin(M_PI * t);
        unsigned char from = (unsigned char)(amp * cos(t * M_PI / 2)), to = (unsigned char)(amp * sin(t * M_PI / 2));
        DODGE_RIGHT.s[i] = (Step){ from, to, ms }; // lo motor = left grip, hi motor = right grip
        DODGE_LEFT.s[i]  = (Step){ to, from, ms };
    }
}

// per-weapon/skill patterns from weapons.txt
#define MAX_ITEMS 400
#define MAX_STRIKES 8
typedef struct { char id[48]; int nstrikes; unsigned char mirror[MAX_STRIKES]; Pattern strike[MAX_STRIKES]; } Item;
static Item items[MAX_ITEMS];
static int nitems;
#define ITEM_PRIO 3

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static Pattern pend; // copied in so callers can pass temporaries
static int has_pending, cur_prio;
static long long busy_until;
static SDL_GameController *gc;
static float intensity = 1.f;
static FILE *logfile;
static int seen[EV_COUNT];
static int last_dir = 1;
static int hd_enabled;

static long long now_ms(void) {
    struct timeval t; gettimeofday(&t, NULL);
    return (long long)t.tv_sec * 1000 + t.tv_usec / 1000;
}

static void rumble(int lo, int hi, int ms) {
    if (hd_alive) return; // HD haptics own the actuators; SDL rumble would switch them back to emulation
    SDL_GameController *c = gc;
    if (c) SDL_GameControllerRumble(c, (unsigned short)(lo * 257 * intensity), (unsigned short)(hi * 257 * intensity), ms);
}

static void *worker(void *_) {
    for (;;) {
        pthread_mutex_lock(&mu);
        while (!has_pending) pthread_cond_wait(&cv, &mu);
        Pattern p = pend; has_pending = 0;
        pthread_mutex_unlock(&mu);
        int interrupted = 0;
        for (int i = 0; i < p.n && !interrupted; i++) {
            rumble(p.s[i].lo, p.s[i].hi, p.s[i].ms + 20); // +20: no gap before the next step lands
            long long end = now_ms() + p.s[i].ms;
            struct timespec ts = { end / 1000, (end % 1000) * 1000000 };
            pthread_mutex_lock(&mu);
            while (!has_pending && now_ms() < end) pthread_cond_timedwait(&cv, &mu, &ts);
            interrupted = has_pending;
            pthread_mutex_unlock(&mu);
        }
        if (!interrupted) rumble(0, 0, 0);
    }
    return NULL;
}

static int ch_enabled = 1; // off when haptics-mod/ch-off exists

// spatial: lo/hi are left/right grip (sweeps, side slashes); otherwise lo/hi mean heavy/light, played centred
static void play(const Pattern *p, int spatial) {
    int total = 0;
    for (int i = 0; i < p->n; i++) total += p->s[i].ms;
    pthread_mutex_lock(&mu);
    long long now = now_ms();
    int accept = now >= busy_until || p->prio >= cur_prio; // never let footsteps cut off a death rumble
    if (accept) { cur_prio = p->prio; busy_until = now + total; }
    pthread_mutex_unlock(&mu);
    if (!accept) return;
    HdStep st[MAX_STEPS];
    for (int i = 0; i < p->n; i++) st[i] = (HdStep){ p->s[i].lo, p->s[i].hi, p->s[i].ms };
    if (hd_alive) { hd_play(st, p->n, intensity); return; }
    if (ch_enabled && ch_play(st, p->n, spatial, intensity)) return;
    pthread_mutex_lock(&mu);
    pend = *p; has_pending = 1;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

// claim the haptics for ms without playing rumble (Core Haptics is playing); SDL rumble would cut it off
static int reserve(int prio, int ms) {
    pthread_mutex_lock(&mu);
    long long now = now_ms();
    int ok = now >= busy_until || prio >= cur_prio;
    if (ok) { cur_prio = prio; busy_until = now + ms; }
    pthread_mutex_unlock(&mu);
    return ok;
}

// line: id [>] lo,hi,ms lo,hi,ms | [>] lo,hi,ms ...   # comment
static void load_items(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[2048];
    while (fgets(line, sizeof line, f) && nitems < MAX_ITEMS) {
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *save, *tok = strtok_r(line, " \t\r\n", &save);
        if (!tok) continue;
        Item *it = &items[nitems];
        memset(it, 0, sizeof *it);
        snprintf(it->id, sizeof it->id, "%s", tok);
        it->nstrikes = 1;
        while ((tok = strtok_r(NULL, " \t\r\n", &save))) {
            Pattern *p = &it->strike[it->nstrikes - 1];
            int lo, hi, ms;
            if (!strcmp(tok, "|")) { if (it->nstrikes < MAX_STRIKES) it->nstrikes++; }
            else if (!strcmp(tok, ">")) it->mirror[it->nstrikes - 1] = 1;
            else if (sscanf(tok, "%d,%d,%d", &lo, &hi, &ms) == 3 && p->n < MAX_STEPS)
                p->s[p->n++] = (Step){ lo < 0 ? 0 : lo > 255 ? 255 : lo, hi < 0 ? 0 : hi > 255 ? 255 : hi, ms < 0 ? 0 : ms > 2000 ? 2000 : ms };
        }
        for (int i = 0; i < it->nstrikes; i++) it->strike[i].prio = ITEM_PRIO;
        if (it->strike[0].n) nitems++;
    }
    fclose(f);
}

static void log_line(const char *msg) {
    if (logfile) { fprintf(logfile, "%s\n", msg); fflush(logfile); }
}

static void init_once(void) {
    build_dodge();
    Dl_info info; char path[1024];
    if (dladdr((void *)init_once, &info) && info.dli_fname) {
        snprintf(path, sizeof path, "%s", info.dli_fname);
        char *slash = strrchr(path, '/');
        size_t dir = slash ? (size_t)(slash - path + 1) : 0;
        snprintf(path + dir, sizeof path - dir, "haptics-mod/intensity.txt");
        FILE *f = fopen(path, "r");
        if (f) { if (fscanf(f, "%f", &intensity) != 1 || intensity < 0 || intensity > 1) intensity = 1; fclose(f); }
        snprintf(path + dir, sizeof path - dir, "haptics-mod/weapons.txt");
        load_items(path);
        snprintf(path + dir, sizeof path - dir, "haptics-mod/hd-on");
        hd_enabled = access(path, F_OK) == 0;
        snprintf(path + dir, sizeof path - dir, "haptics-mod/ch-off");
        ch_enabled = access(path, F_OK) != 0; // experimental HD haptics are opt-in (create the empty file)
        snprintf(path + dir, sizeof path - dir, "haptics-mod/haptics.log");
        logfile = fopen(path, "w");
    }
    if (logfile) { fprintf(logfile, "haptics loaded, intensity %.2f, %d weapon patterns, core haptics %s\n", intensity, nitems, ch_enabled ? "on" : "off"); fflush(logfile); }
    pthread_t t; pthread_create(&t, NULL, worker, NULL); pthread_detach(t);
    if (hd_enabled) hd_start(log_line);
}

static void find_controller(void) {
    if (gc && SDL_GameControllerGetAttached(gc)) return;
    SDL_GameController *best = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController *c = SDL_GameControllerOpen(i); // refcounted: returns the game's handle
        if (c && (!best || SDL_GameControllerGetType(c) == SDL_CONTROLLER_TYPE_PS5)) best = c;
    }
    if (best != gc && logfile) { fprintf(logfile, "controller: %s\n", best ? SDL_GameControllerName(best) : "none"); fflush(logfile); }
    gc = best;
}

static void prepare(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_once);
    find_controller();
    if (!hd_alive && ch_enabled) ch_warm();
    // track the last horizontal input so a roll/slash with the stick centered still knows the facing side
    if (gc) {
        int x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        if (x > 12000 || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) last_dir = 1;
        else if (x < -12000 || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) last_dir = -1;
    }
}

void dch_event(int id) {
    prepare();
    if (id < 0 || id >= EV_COUNT) return;
    // DiveAttack.onOwnerLand runs on every landing; only count it right after a dive started
    static long long last_dive;
    if (id == EV_DIVE) last_dive = now_ms();
    if (id == EV_DIVE_LAND && now_ms() - last_dive > 2000) return;
    if (id == EV_DIVE_LAND) last_dive = 0;
    if (logfile && seen[id]++ < 3) { fprintf(logfile, "event %s\n", EV_NAMES[id]); fflush(logfile); }
    const Pattern *p = &PATTERNS[id];
    if (id == EV_DODGE || id == EV_DODGE_RICHTER) {
        static int ch_logged;
        if (!hd_alive && ch_enabled && reserve(PATTERNS[EV_DODGE].prio, 300) && ch_dash(last_dir, intensity)) {
            if (!ch_logged++) log_line("ch: dodge dash via Core Haptics");
            return;
        }
        p = last_dir < 0 ? &DODGE_LEFT : &DODGE_RIGHT;
    }
    play(p, id == EV_SCEPTER_SPIN);
}

// id: HashLink string bytes (UTF-16), strike: weapon combo step (0 for skills)
void dch_item(const unsigned short *wid, int strike) {
    prepare();
    if (!wid) return;
    char id[48]; int i = 0;
    for (; wid[i] && i < 47; i++) id[i] = wid[i] < 128 ? (char)wid[i] : '?';
    id[i] = 0;
    static int logged;
    if (logfile && logged < 60) { logged++; fprintf(logfile, "item %s strike %d\n", id, strike); fflush(logfile); }
    static const Pattern fallback = P(ITEM_PRIO, S(50, 110, 50));
    const Item *it = NULL;
    for (int k = 0; k < nitems; k++) if (!strcmp(items[k].id, id)) { it = &items[k]; break; }
    if (!it) { play(&fallback, 0); return; }
    int s = (strike < 0 ? 0 : strike) % it->nstrikes;
    Pattern p = it->strike[s];
    if (it->mirror[s] && last_dir < 0) // patterns are authored facing right: swap grips when facing left
        for (int k = 0; k < p.n; k++) { unsigned char t = p.s[k].lo; p.s[k].lo = p.s[k].hi; p.s[k].hi = t; }
    play(&p, it->mirror[s]);
}

__attribute__((visibility("default"))) void *hlp_dch_event(const char **sign) {
    *sign = "Pi_v";
    return (void *)dch_event;
}

__attribute__((visibility("default"))) void *hlp_dch_item(const char **sign) {
    *sign = "PBi_v";
    return (void *)dch_item;
}
