#include "target.h"

#include "protocol.h"
#include "state.h"
#include "ui.h"

// How long after the last nudge to wait for the server to report the expected
// target before giving up and reverting. Telemetry reaches the watch through
// the phone's 1 s outbox and a 1 s SignalK subscription, so a confirmation
// normally lands within ~2-3 s.
#define CONFIRM_TIMEOUT_MS 5000

// Server and displayed values may differ by rounding (radians -> whole degrees).
#define CONFIRM_TOLERANCE_DEG 1

// Which way a heading nudge moves the target wind angle in WIND mode. +1
// matches dev/signalk-autopilot-mock (target wind angle += delta).
// TODO(hardware): confirm against the real signalk-autopilot plugin; if the
// target wind angle moves the other way, set this to -1. A wrong sign shows up
// as a "REVERTED" alert after every WIND-mode nudge.
#define WIND_ADJUST_SIGN 1

static int s_server_heading = TARGET_NONE;  // latest values reported by the server
static int s_server_wind = TARGET_NONE;
static int s_pending_mode = -1;  // AP_STATE_AUTO / AP_STATE_WIND while a change is pending
static AppTimer* s_timer;

static int norm360(int deg) {
    return ((deg % 360) + 360) % 360;
}

static int* displayed(int mode) {
    AppState* state = app_state();
    return mode == AP_STATE_WIND ? &state->target_wind : &state->target_heading;
}

static int* server(int mode) {
    return mode == AP_STATE_WIND ? &s_server_wind : &s_server_heading;
}

static bool matches(int a, int b) {
    int d = norm360(a - b);
    if (d > 180) d = 360 - d;
    return d <= CONFIRM_TOLERANCE_DEG;
}

// Drop the pending change and show what the server last reported.
static void clear_pending(void) {
    if (s_timer) {
        app_timer_cancel(s_timer);
        s_timer = NULL;
    }
    if (s_pending_mode != -1) {
        *displayed(s_pending_mode) = *server(s_pending_mode);
        s_pending_mode = -1;
        ui_mark_dirty();
    }
}

static void revert(const char* label) {
    clear_pending();
    ui_set_feedback(label);
    vibes_long_pulse();
}

static void confirm_timeout(void* data) {
    s_timer = NULL;
    revert("REVERTED");
}

void target_on_adjust_sent(int delta) {
    const int mode = app_state()->autopilot_state;
    if (mode == AP_STATE_AUTO || mode == AP_STATE_WIND) {
        if (s_pending_mode != -1 && s_pending_mode != mode) clear_pending();
        int* t = displayed(mode);
        if (*t != TARGET_NONE) {
            *t = norm360(*t + (mode == AP_STATE_WIND ? WIND_ADJUST_SIGN * delta : delta));
            s_pending_mode = mode;
            if (s_timer) app_timer_cancel(s_timer);
            s_timer = app_timer_register(CONFIRM_TIMEOUT_MS, confirm_timeout, NULL);
            ui_mark_dirty();
        }
    }
}

void target_on_server(int mode, int value) {
    const int previous = *server(mode);
    *server(mode) = value;
    if (value == TARGET_NONE && s_pending_mode == mode) clear_pending();
    if (s_pending_mode == mode) {
        // Still waiting: ignore stale values, confirm on the expected one.
        if (value == *displayed(mode) || (value != previous && matches(value, *displayed(mode)))) {
            *displayed(mode) = value;
            if (s_timer) {
                app_timer_cancel(s_timer);
                s_timer = NULL;
            }
            s_pending_mode = -1;
        }
        return;
    }
    *displayed(mode) = value;
}

void target_on_mode(int ap_state) {
    if (s_pending_mode != -1 && s_pending_mode != ap_state) clear_pending();
}

bool target_on_failure(void) {
    if (s_pending_mode == -1) return false;
    revert("FAILED");
    return true;
}
