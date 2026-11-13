// DualSense HD haptics over Bluetooth (macOS): streams 8-bit stereo 3 kHz audio to the two voice-coil
// actuators with output report 0x32 (double-frame layout from DS5Dongle, CRC from SAxense).
// Used only for a Bluetooth DualSense; haptics.c falls back to SDL rumble whenever hd_alive is 0.
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/IOKitLib.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hd.h"

#define RATE 3000
#define BLOCK 64            // stereo frames per report = 21.33 ms
#define MAX_FRAMES (RATE * 3)
#define MAX_INFLIGHT 3      // ~64 ms queued in macOS: keeps new effects able to cut in quickly
#define CTRL_BUFFER 128     // controller jitter buffer; 128 felt smoother than 48 in blind tests

volatile int hd_alive;
static IOHIDDeviceRef dev;
static int inflight, fails;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int8_t next_buf[MAX_FRAMES * 2], cur_buf[MAX_FRAMES * 2];
static int next_len = -1, cur_len, cur_pos, need_state;
static uint8_t ring[16][142];
static int slot;
static uint8_t seq, ctr;
static void (*logfn)(const char *);

static uint32_t crc32_a2(const uint8_t *d, size_t n) {
    uint32_t c = ~0xEADA2D49u; // CRC state after the 0xA2 Bluetooth output header byte
    while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
    return ~c;
}

static void done_cb(void *ctx, IOReturn r, void *sender, IOHIDReportType t, uint32_t id, uint8_t *rep, CFIndex len) {
    inflight--;
    fails = r ? fails + 1 : 0;
    if (fails >= 10 && hd_alive) { hd_alive = 0; if (logfn) logfn("hd: writes failing, back to rumble"); }
}

static void send(uint8_t *r) {
    uint32_t c = crc32_a2(r, 138);
    memcpy(r + 138, &c, 4);
    if (IOHIDDeviceSetReportWithCallback(dev, kIOHIDReportTypeOutput, 0x32, r, 142, 1.0, done_cb, NULL) == kIOReturnSuccess) inflight++;
    else if (++fails >= 10 && hd_alive) { hd_alive = 0; if (logfn) logfn("hd: send rejected, back to rumble"); }
}

// packet 0x10 = DS5 set-state: valid_flag0 0xFD selects audio haptics instead of rumble emulation.
// valid_flag1 = 0 so the game's lightbar / player LEDs are left alone.
static void send_state(void) {
    static const uint8_t state[] = {0x90, 0x3f, 0xfd, 0x00, 0x0, 0x0, 0x7f, 0x7f, 0xff, 0x9};
    uint8_t *r = ring[slot++ & 15];
    memset(r, 0, 142);
    r[0] = 0x32; r[1] = 0x10;
    memcpy(r + 2, state, sizeof state);
    send(r);
}

static void send_block(const int8_t *lr) {
    uint8_t *r = ring[slot++ & 15];
    memset(r, 0, 142);
    r[0] = 0x32; r[1] = (uint8_t)((seq++ & 15) << 4);
    r[2] = 0x91; r[3] = 4; r[4] = 0x66; r[5] = CTRL_BUFFER; r[6] = CTRL_BUFFER; r[7] = ctr; ctr += 2; // packet 0x11
    r[8] = 0xD2; r[9] = 64;                                                                            // packet 0x12, 2 frames
    memcpy(r + 10, lr, BLOCK * 2);
    send(r);
}

static int is_bt_dualsense(io_service_t s) {
    int pid = 0; char tr[32] = "";
    CFTypeRef p = IORegistryEntryCreateCFProperty(s, CFSTR(kIOHIDProductIDKey), kCFAllocatorDefault, 0);
    CFTypeRef t = IORegistryEntryCreateCFProperty(s, CFSTR(kIOHIDTransportKey), kCFAllocatorDefault, 0);
    if (p) { CFNumberGetValue(p, kCFNumberIntType, &pid); CFRelease(p); }
    if (t) { CFStringGetCString(t, tr, sizeof tr, kCFStringEncodingUTF8); CFRelease(t); }
    return (pid == 0x0CE6 || pid == 0x0DF2) && !strcmp(tr, "Bluetooth");
}

static void try_open(void) {
    if (dev) { IOHIDDeviceUnscheduleFromRunLoop(dev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode); IOHIDDeviceClose(dev, 0); CFRelease(dev); dev = NULL; }
    CFMutableDictionaryRef match = IOServiceMatching(kIOHIDDeviceKey);
    int vid = 0x054C;
    CFNumberRef v = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFDictionarySetValue(match, CFSTR(kIOHIDVendorIDKey), v);
    CFRelease(v);
    io_iterator_t it;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) != KERN_SUCCESS) return;
    io_service_t s;
    while (!dev && (s = IOIteratorNext(it))) {
        if (is_bt_dualsense(s)) {
            dev = IOHIDDeviceCreate(kCFAllocatorDefault, s);
            if (dev && IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone) != kIOReturnSuccess) { CFRelease(dev); dev = NULL; }
        }
        IOObjectRelease(s);
    }
    IOObjectRelease(it);
    if (!dev) return;
    IOHIDDeviceScheduleWithRunLoop(dev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    inflight = fails = 0;
    hd_alive = 1;
    if (logfn) logfn("hd: Bluetooth DualSense opened, HD haptics on");
}

static void *thread(void *_) {
    int last_try = -100000, t = 0;
    for (;;) {
        if (!hd_alive) {
            if (t - last_try >= 3000) { last_try = t; try_open(); } // retry: controller may reconnect
            if (!hd_alive) { usleep(20000); t += 20; continue; }
        }
        pthread_mutex_lock(&mu);
        if (next_len >= 0) { memcpy(cur_buf, next_buf, next_len * 2); cur_len = next_len; cur_pos = 0; next_len = -1; need_state = 1; }
        pthread_mutex_unlock(&mu);
        while (hd_alive && inflight < MAX_INFLIGHT && (need_state || cur_pos < cur_len)) {
            if (need_state) { send_state(); need_state = 0; continue; }
            int8_t blk[BLOCK * 2] = {0};
            int n = cur_len - cur_pos < BLOCK ? cur_len - cur_pos : BLOCK;
            memcpy(blk, cur_buf + cur_pos * 2, n * 2);
            cur_pos += n;
            send_block(blk);
        }
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
        t += 2;
    }
    return NULL;
}

void hd_start(void (*log)(const char *)) {
    logfn = log;
    pthread_t t;
    pthread_create(&t, NULL, thread, NULL);
    pthread_detach(t);
}

// Rolling-rumble texture (picked over a pure tone in A/B tests): low-passed noise + 55 Hz body,
// amplitude-grained at 14 Hz like something tumbling. Same texture in both grips so panning reads as one source.
static double texture(long i) {
    static double lp1, lp2;
    double n = (double)rand() / RAND_MAX * 2 - 1;
    lp1 += 0.12 * (n - lp1); lp2 += 0.12 * (lp1 - lp2);
    double v = (0.55 * sin(2 * M_PI * 55 * i / RATE) + 3.2 * lp2) * (0.7 + 0.3 * sin(2 * M_PI * 14 * i / RATE));
    return v > 1 ? 1 : v < -1 ? -1 : v;
}

// steps: left/right strength 0-255 for ms. Amplitudes glide (~6 ms) so stepped patterns become continuous.
void hd_play(const HdStep *s, int n, float gain) {
    static int8_t buf[MAX_FRAMES * 2];
    static long phase;
    const double k = 1 - exp(-1.0 / (0.006 * RATE));
    double aL = 0, aR = 0;
    int f = 0;
    for (int i = 0; i <= n && f < MAX_FRAMES; i++) {
        int frames = i < n ? s[i].ms * RATE / 1000 : RATE / 40; // trailing 25 ms lets the glide fade out
        double tL = i < n ? s[i].left / 255.0 : 0, tR = i < n ? s[i].right / 255.0 : 0;
        for (int j = 0; j < frames && f < MAX_FRAMES; j++, f++) {
            aL += k * (tL - aL); aR += k * (tR - aR);
            double x = texture(phase++) * 125 * gain;
            buf[2 * f] = (int8_t)(x * aL);
            buf[2 * f + 1] = (int8_t)(x * aR);
        }
    }
    pthread_mutex_lock(&mu);
    memcpy(next_buf, buf, f * 2);
    next_len = f;
    pthread_mutex_unlock(&mu);
}
