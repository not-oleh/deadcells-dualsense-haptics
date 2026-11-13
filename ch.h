#pragma once
#include "hd.h" // HdStep: left/right strength 0-255 + ms
// Plays the dodge "dash" via Core Haptics; dir 1 = left->right, -1 = right->left. Returns 0 if unavailable.
int ch_dash(int dir, float intensity);
// Plays stepped pattern; spatial: left->left grip, right->right grip, else centred (both grips) with
// sharpness from the heavy/light mix. Stops the previous ch_play effect. Returns 0 if unavailable.
int ch_play(const HdStep *s, int n, int spatial, float gain);
void ch_warm(void); // call often; sets up engines as soon as the controller is visible
