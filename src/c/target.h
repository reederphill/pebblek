/*
 * Optimistic autopilot target.
 *
 * A heading nudge updates the displayed target (heading in AUTO, wind angle in
 * WIND) immediately and assumes it will be applied. Telemetry carrying the old
 * target — which keeps arriving until the autopilot catches up — is ignored
 * while the change is pending. The pending change is confirmed when the server
 * reports the expected target, and reverted (with an alert) when the phone
 * reports the command failed or no confirmation arrives in time.
 */

#pragma once

#include <pebble.h>

// Update the displayed target after submitting a relative adjustment.
void target_on_adjust_sent(int delta);

// A target value arrived from the phone. mode is AP_STATE_AUTO for
// TARGET_HEADING, AP_STATE_WIND for TARGET_WIND_ANGLE.
void target_on_server(int mode, int value);

// The autopilot state arrived from the phone; drops a pending change made in a
// different mode.
void target_on_mode(int ap_state);

// A command failed (CMD_RESULT FAILED, or the AppMessage wasn't delivered). If
// an optimistic change was pending, reverts it, alerts and returns true.
bool target_on_failure(void);
