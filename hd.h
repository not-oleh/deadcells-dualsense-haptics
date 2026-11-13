#pragma once
typedef struct { unsigned char left, right; unsigned short ms; } HdStep;
extern volatile int hd_alive;                  // 1 while a Bluetooth DualSense is streaming HD haptics
void hd_start(void (*log)(const char *));      // background thread: opens the controller, retries on reconnect
void hd_play(const HdStep *s, int n, float gain); // replaces whatever HD effect is playing
