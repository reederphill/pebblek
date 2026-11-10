#include "buttons.h"

#include "comm.h"
#include "dial.h"
#include "protocol.h"
#include "state.h"
#include "ui.h"

// Holding UP/DOWN past the long-press delay steps the heading by HOLD_STEP and
// keeps stepping every HOLD_REPEAT_MS until the button is released.
#define HOLD_STEP 10
#define HOLD_REPEAT_MS 400

static AppTimer* s_hold_timer;
static int s_hold_dir;    // +1 (UP) or -1 (DOWN) while holding

// A heading nudge. Presses are batched by the dial: they add up in a pending
// change that is sent 1 s after the last press (or on SELECT), so the target
// doesn't move underneath you while you're still counting.
static void adjust(int delta) {
    if (!ui_is_pilot() || !state_fresh(FIELD_AUTOPILOT)) return;
    dial_nudge(delta);
}

// A very short buzz per hold step, so steps can be counted without looking.
static void tick_vibe(void) {
    static const uint32_t segments[] = {30};
    vibes_enqueue_custom_pattern((VibePattern){.durations = segments, .num_segments = 1});
}

static void hold_step(void* data) {
    s_hold_timer = NULL;
    if (!ui_is_pilot() || !state_fresh(FIELD_AUTOPILOT)) {
        buttons_cancel();
        return;
    }
    const int delta = s_hold_dir * HOLD_STEP;
    adjust(delta);
    tick_vibe();
    s_hold_timer = app_timer_register(HOLD_REPEAT_MS, hold_step, NULL);
}

static void hold_start(int dir) {
    if (s_hold_timer) app_timer_cancel(s_hold_timer);
    s_hold_dir = dir;
    app_state()->holding = true;
    hold_step(NULL);
}

void buttons_cancel(void) {
    if (s_hold_timer) {
        app_timer_cancel(s_hold_timer);
        s_hold_timer = NULL;
    }
    app_state()->holding = false;
}

static void hold_end(ClickRecognizerRef r, void* c) {
    buttons_cancel();
}

static void up_click(ClickRecognizerRef r, void* c) {
    adjust(1);
}
static void up_long(ClickRecognizerRef r, void* c) {
    hold_start(+1);
}
static void down_click(ClickRecognizerRef r, void* c) {
    adjust(-1);
}
static void down_long(ClickRecognizerRef r, void* c) {
    hold_start(-1);
}
// SELECT engages/disengages the autopilot. When already engaged (any mode), a
// press disengages. When in standby, the chosen mode is engaged: a single click
// engages AUTO, a long press engages WIND.
static void engage(ApState mode_when_standby) {
    if (!ui_is_pilot() || !state_fresh(FIELD_AUTOPILOT)) return;
    // A pending dial owns SELECT: it sends the change (or cancels a zero one),
    // and must never disengage the autopilot by accident.
    if (dial_is_active()) {
        dial_send();
        return;
    }
    const int ap = app_state()->autopilot_state;
    const bool engaged = (ap != AP_STATE_STANDBY && ap != AP_STATE_UNKNOWN);
    const ApState target = engaged ? AP_STATE_STANDBY : mode_when_standby;
    comm_set_state(target);
}
static void select_click(ClickRecognizerRef r, void* c) {
    engage(AP_STATE_AUTO);
}
static void select_long(ClickRecognizerRef r, void* c) {
    if (dial_is_active()) return;
    engage(AP_STATE_WIND);
}

// One press cancels unsent changes and returns to the instruments.
static void back_click(ClickRecognizerRef r, void* c) {
    buttons_cancel();
    dial_cancel();
    window_stack_pop(true);
}

void buttons_click_config_provider(void* context) {
    window_single_click_subscribe(BUTTON_ID_UP, up_click);
    window_long_click_subscribe(BUTTON_ID_UP, 0, up_long, hold_end);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
    window_long_click_subscribe(BUTTON_ID_DOWN, 0, down_long, hold_end);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
    window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long, NULL);
    window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}
