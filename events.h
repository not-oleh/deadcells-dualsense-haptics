#pragma once
// Event ids and names, from events.def (shared with dchpatch.c, which decides what gets hooked)
enum {
#define EV(n, f) EV_##n,
#include "events.def"
#undef EV
    EV_COUNT
};
static const char *EV_NAMES[] = {
#define EV(n, f) #n,
#include "events.def"
#undef EV
};
