#include "dial.h"

#include "comm.h"
#include "dial_math.h"
#include "protocol.h"
#include "state.h"
#include "ui.h"

// A change dialled by touch that nobody confirms is dropped after this long.
#define DIAL_TIMEOUT_MS 10000
// A change built from UP/DOWN presses is sent this long after the last press.
#define DIAL_AUTO_SEND_MS 1000

static AppTimer* s_timeout;
static bool s_auto;  // pending change came from buttons: auto-sends, not dropped
static bool s_dragging;
static int s_start_x;
static int s_base_delta;  // pending change when the current drag started

static bool engaged(void) {
    const int ap = app_state()->autopilot_state;
    return ui_is_pilot() && state_fresh(FIELD_AUTOPILOT) &&
           ((ap == AP_STATE_AUTO && state_fresh(FIELD_TARGET_HEADING)) ||
            (ap == AP_STATE_WIND && state_fresh(FIELD_TARGET_WIND)));
}

static void expire(void* data) {
    s_timeout = NULL;
    if (s_auto) {
        dial_send();
    } else {
        dial_cancel();
    }
}

static void restart_timeout(void) {
    const uint32_t ms = s_auto ? DIAL_AUTO_SEND_MS : DIAL_TIMEOUT_MS;
    if (s_timeout) {
        app_timer_reschedule(s_timeout, ms);
    } else {
        s_timeout = app_timer_register(ms, expire, NULL);
    }
}

void dial_cancel(void) {
    AppState* state = app_state();
    if (s_timeout) {
        app_timer_cancel(s_timeout);
        s_timeout = NULL;
    }
    s_dragging = false;
    s_auto = false;
    state->dial_active = false;
    state->dial_delta = 0;
    ui_mark_dirty();
}

bool dial_is_active(void) {
    AppState* state = app_state();
    if (state->dial_active && !engaged()) dial_cancel();
    return state->dial_active;
}

void dial_nudge(int delta) {
    if (!ui_is_pilot() || !state_fresh(FIELD_AUTOPILOT)) return;
    AppState* state = app_state();
    if (!dial_is_active()) {
        if (!engaged()) return;
        // Start a batch: presses add up, and it is sent once they stop.
        state->dial_active = true;
        s_auto = true;
    }
    state->dial_delta = dial_clamp(state->dial_delta + delta);
    restart_timeout();
    ui_mark_dirty();
}

void dial_send(void) {
    const int delta = engaged() ? app_state()->dial_delta : 0;
    dial_cancel();
    if (delta != 0) comm_adjust_heading(delta);
}

static void touch_handler(const TouchEvent* e, void* context) {
    if (!ui_is_pilot()) return;
    AppState* state = app_state();
    switch (e->type) {
        case TouchEvent_Touchdown:
            // Only while connected and engaged, and not on the header.
            if (e->y < 36) return;
            if (e->non_navigational || state->conn != CONN_CONNECTED || !engaged()) return;
            state->dial_active = true;
            s_auto = false;  // touching takes over: now SELECT sends, as for any drag
            s_dragging = true;
            s_start_x = e->x;
            s_base_delta = state->dial_delta;
            restart_timeout();
            ui_mark_dirty();
            break;
        case TouchEvent_PositionUpdate:
            if (!s_dragging) return;
            state->dial_delta = dial_from_drag(s_base_delta, e->x - s_start_x);
            restart_timeout();
            ui_mark_dirty();
            break;
        case TouchEvent_Liftoff:
            s_dragging = false;
            break;
    }
}

void dial_init(void) {
    touch_service_subscribe(touch_handler, NULL);
}

void dial_deinit(void) {
    touch_service_unsubscribe();
    dial_cancel();
}
