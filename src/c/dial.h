/*
 * Touch "dial": dial in a larger heading / wind-angle change, then send it.
 *
 *   - Touch and drag left/right below the header to dial a pending change
 *     (port = negative, starboard = positive; see dial_math.h). Nothing is sent.
 *   - UP/DOWN add to the pending change (+/-1, hold for +/-10). If there was none,
 *     they start one that is sent automatically 1 s after the last press, so a
 *     run of presses lands as a single adjustment.
 *   - SELECT sends the pending change at once; BACK cancels it and goes home. A change dialled
 *     by touch is not auto-sent: it waits for SELECT, and is dropped after 10 s.
 *
 * The pending change lives in app_state() (dial_active / dial_delta) so the UI can
 * preview it. Only available on the pilot page with fresh AUTO/WIND status and target.
 */

#pragma once

#include <pebble.h>

// Subscribe to touch events. Call once at startup.
void dial_init(void);
void dial_deinit(void);

// True while a pending change exists (even if it is currently 0). Cancels the
// dial first if the autopilot is no longer engaged.
bool dial_is_active(void);

// Add to the pending change, starting an auto-sending one if there is none
// (UP/DOWN). Ignored unless the pilot is engaged with a fresh target.
void dial_nudge(int delta);

// Send the pending change (a no-op cancel if it is 0), then close the dial.
void dial_send(void);

// Discard the pending change.
void dial_cancel(void);
