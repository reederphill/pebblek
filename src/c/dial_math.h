/*
 * Pure math for the touch dial, kept free of Pebble headers so it can be unit
 * tested on the host (see CLAUDE.md).
 */

#pragma once

#define DIAL_SNAP 5   // a drag dials in steps of this many degrees
#define DIAL_MAX 180  // a pending change is clamped to +/- this many degrees

static inline int dial_clamp(int delta) {
    if (delta > DIAL_MAX) return DIAL_MAX;
    if (delta < -DIAL_MAX) return -DIAL_MAX;
    return delta;
}

// The pending change after dragging dx pixels (right = positive = starboard) on
// top of a base change: 1 degree per pixel, rounded to the nearest DIAL_SNAP.
static inline int dial_from_drag(int base, int dx) {
    const int half = DIAL_SNAP / 2;
    const int snapped = ((dx >= 0 ? dx + half : dx - half) / DIAL_SNAP) * DIAL_SNAP;
    return dial_clamp(base + snapped);
}
