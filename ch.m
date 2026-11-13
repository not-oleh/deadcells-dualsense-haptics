// Dodge-roll "dash" through Apple's GameController + Core Haptics (the API behind System Settings' Identify).
// Three overlapping continuous events: start grip -> both grips (felt in the middle) -> end grip.
// Picked in blind tests (5/5 directions read correctly) over hard steps and taps.
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#include "ch.h"

static GCController *ctl;
static CHHapticEngine *engL, *engR, *engB;

static CHHapticEngine *make_engine(GCHapticsLocality loc) {
    CHHapticEngine *e = [ctl.haptics createEngineWithLocality:loc];
    if (!e) return nil;
    e.autoShutdownEnabled = NO;
    e.playsHapticsOnly = YES;
    __weak CHHapticEngine *w = e;
    e.resetHandler = ^{ [w startAndReturnError:nil]; }; // e.g. after the controller reconnects
    if (![e startAndReturnError:nil]) return nil;
    return e;
}

static int ready(void) {
    GCController.shouldMonitorBackgroundEvents = YES; // also lets the A/B test tool play from the background
    GCController *c = nil;
    for (GCController *x in GCController.controllers) if (x.haptics) { c = x; break; }
    if (!c) { ctl = nil; engL = engR = engB = nil; return 0; }
    if (c != ctl) {
        ctl = c;
        engL = make_engine(GCHapticsLocalityLeftHandle);
        engR = make_engine(GCHapticsLocalityRightHandle);
        engB = make_engine(GCHapticsLocalityHandles);
    }
    return engL && engR && engB;
}

// one continuous event with a linear intensity fade in/out (seconds)
static CHHapticPattern *segment(double dur, double fade_in, double fade_out, float intensity, float sharpness) {
    CHHapticEvent *ev = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous parameters:@[
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:intensity],
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:sharpness]]
        relativeTime:0 duration:dur];
    NSMutableArray *pts = [NSMutableArray array];
    [pts addObject:[[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:0 value:fade_in > 0 ? 0 : 1]];
    if (fade_in > 0) [pts addObject:[[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:fade_in value:1]];
    if (fade_out > 0) [pts addObject:[[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:dur - fade_out value:1]];
    [pts addObject:[[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:dur value:fade_out > 0 ? 0 : 1]];
    CHHapticParameterCurve *cv = [[CHHapticParameterCurve alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                                                                       controlPoints:pts relativeTime:0];
    return [[CHHapticPattern alloc] initWithEvents:@[ev] parameterCurves:@[cv] error:nil];
}

static int start(CHHapticEngine *e, CHHapticPattern *p, double at) {
    NSError *err = nil;
    id<CHHapticPatternPlayer> pl = [e createPlayerWithPattern:p error:&err];
    return pl && [pl startAtTime:e.currentTime + at error:&err];
}

static NSArray *playing;

static CHHapticEventParameter *param(CHHapticEventParameterID i, float v) {
    return [[CHHapticEventParameter alloc] initWithParameterID:i value:v];
}

static void add(NSMutableArray *evs, double t, double dur, float intensity, float sharpness) {
    if (intensity < 0.02) return;
    NSArray *ps = @[param(CHHapticEventParameterIDHapticIntensity, intensity), param(CHHapticEventParameterIDHapticSharpness, sharpness)];
    if (dur <= 0.030) // very short steps (footsteps, coins) feel better as one crisp tap
        [evs addObject:[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticTransient parameters:ps relativeTime:t]];
    else
        [evs addObject:[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous parameters:ps relativeTime:t duration:dur]];
}

static id<CHHapticPatternPlayer> run(CHHapticEngine *e, NSArray *evs) {
    if (!evs.count) return nil;
    NSError *err = nil;
    CHHapticPattern *p = [[CHHapticPattern alloc] initWithEvents:evs parameters:@[] error:&err];
    id<CHHapticPatternPlayer> pl = p ? [e createPlayerWithPattern:p error:&err] : nil;
    return pl && [pl startAtTime:CHHapticTimeImmediate error:&err] ? pl : nil;
}

int ch_play(const HdStep *s, int n, int spatial, float gain) {
    @autoreleasepool {
        if (!ready()) return 0;
        for (id<CHHapticPatternPlayer> p in playing) [p stopAtTime:CHHapticTimeImmediate error:nil];
        NSMutableArray *evL = [NSMutableArray array], *evR = [NSMutableArray array], *evB = [NSMutableArray array];
        double t = 0;
        for (int i = 0; i < n; i++) {
            float lo = s[i].left / 255.f * gain, hi = s[i].right / 255.f * gain;
            double dur = s[i].ms / 1000.0;
            if (spatial) { add(evL, t, dur, lo, 0.5f); add(evR, t, dur, hi, 0.5f); }
            else add(evB, t, dur, fmaxf(lo, hi), lo + hi > 0 ? 0.15f + 0.75f * hi / (lo + hi) : 0.5f); // heavy = dull, light = crisp
            t += dur;
        }
        NSMutableArray *now = [NSMutableArray array];
        id<CHHapticPatternPlayer> p;
        if ((p = run(engL, evL))) [now addObject:p];
        if ((p = run(engR, evR))) [now addObject:p];
        if ((p = run(engB, evB))) [now addObject:p];
        playing = now;
        return 1;
    }
}

void ch_warm(void) {
    @autoreleasepool { if (!ctl) ready(); } // cheap until the controller shows up, then a no-op
}

int ch_dash(int dir, float intensity) {
    @autoreleasepool {
        if (!ready()) return 0;
        static CHHapticPattern *first, *mid, *last;
        static float made_for = -1;
        if (made_for != intensity) { // ms: start grip 0-160 fading out, middle 70-230, end grip 140-300 fading in
            first = segment(0.160, 0, 0.100, intensity, 0.5);
            mid = segment(0.160, 0.060, 0.060, intensity, 0.5);
            last = segment(0.160, 0.100, 0, intensity, 0.5);
            made_for = intensity;
        }
        CHHapticEngine *from = dir < 0 ? engR : engL, *to = dir < 0 ? engL : engR;
        return start(from, first, 0.0) & start(engB, mid, 0.070) & start(to, last, 0.140);
    }
}
