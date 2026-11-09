#include "buttons.h"

#include "comm.h"
#include "protocol.h"
#include "state.h"
#include "ui.h"

// Holding UP/DOWN past the long-press delay steps the heading by HOLD_STEP and
// keeps stepping every HOLD_REPEAT_MS until the button is released.
#define HOLD_STEP 10
#define HOLD_REPEAT_MS 400

static AppTimer* s_hold_timer;
static int s_hold_dir;    // +1 (UP) or -1 (DOWN) while holding
static int s_hold_total;  // degrees sent so far in the current hold

// Send a heading nudge. In AUTO the target heading is updated optimistically so
// the SET readout responds instantly; the phone's telemetry corrects it.
static void adjust(int delta) {
    AppState* state = app_state();
    if (state->autopilot_state == AP_STATE_AUTO && state->target_heading != TARGET_NONE) {
        state->target_heading = ((state->target_heading + delta) % 360 + 360) % 360;
    }
    comm_adjust_heading(delta);
}

static void show_delta(int delta) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%d°", delta >= 0 ? "+" : "-", delta >= 0 ? delta : -delta);
    ui_set_feedback(buf);
}

// A very short buzz per hold step, so steps can be counted without looking.
static void tick_vibe(void) {
    static const uint32_t segments[] = {30};
    vibes_enqueue_custom_pattern((VibePattern){.durations = segments, .num_segments = 1});
}

static void hold_step(void* data) {
    const int delta = s_hold_dir * HOLD_STEP;
    s_hold_total += delta;
    adjust(delta);
    show_delta(s_hold_total);
    tick_vibe();
    s_hold_timer = app_timer_register(HOLD_REPEAT_MS, hold_step, NULL);
}

static void hold_start(int dir) {
    if (s_hold_timer) app_timer_cancel(s_hold_timer);
    s_hold_dir = dir;
    s_hold_total = 0;
    app_state()->holding = true;
    hold_step(NULL);
}

static void hold_end(ClickRecognizerRef r, void* c) {
    if (s_hold_timer) {
        app_timer_cancel(s_hold_timer);
        s_hold_timer = NULL;
    }
    app_state()->holding = false;
    show_delta(s_hold_total);  // keep the total on screen for a few seconds
}

static void up_click(ClickRecognizerRef r, void* c) {
    adjust(1);
    show_delta(1);
}
static void up_long(ClickRecognizerRef r, void* c) {
    hold_start(+1);
}
static void down_click(ClickRecognizerRef r, void* c) {
    adjust(-1);
    show_delta(-1);
}
static void down_long(ClickRecognizerRef r, void* c) {
    hold_start(-1);
}
// SELECT engages/disengages the autopilot. When already engaged (any mode), a
// press disengages. When in standby, the chosen mode is engaged: a single click
// engages AUTO, a long press engages WIND.
static void engage(ApState mode_when_standby, const char* engage_label) {
    const int ap = app_state()->autopilot_state;
    const bool engaged = (ap != AP_STATE_STANDBY && ap != AP_STATE_UNKNOWN);
    const ApState target = engaged ? AP_STATE_STANDBY : mode_when_standby;
    comm_set_state(target);
    ui_set_feedback(engaged ? "DISENGAGE" : engage_label);
}
static void select_click(ClickRecognizerRef r, void* c) {
    engage(AP_STATE_AUTO, "AUTO");
}
static void select_long(ClickRecognizerRef r, void* c) {
    engage(AP_STATE_WIND, "WIND");
}

void buttons_click_config_provider(void* context) {
    window_single_click_subscribe(BUTTON_ID_UP, up_click);
    window_long_click_subscribe(BUTTON_ID_UP, 0, up_long, hold_end);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
    window_long_click_subscribe(BUTTON_ID_DOWN, 0, down_long, hold_end);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
    window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long, NULL);
}
