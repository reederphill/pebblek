#include <assert.h>
#include "state.h"
#include "protocol.h"
#include "target.h"
#include "ui.h"

struct AppTimer { void (*fire)(void*); };
static AppTimer timer;
static const char* feedback;
static unsigned buzzes;

AppTimer* app_timer_register(uint32_t ms, void (*callback)(void*), void* data) {
    assert(ms == 5000 && !data);
    timer.fire = callback;
    return &timer;
}

void app_timer_cancel(AppTimer* t) {
    assert(t == &timer);
    timer.fire = NULL;
}

void ui_mark_dirty(void) {}
void ui_set_feedback(const char* label) { feedback = label; }
void vibes_long_pulse(void) { ++buzzes; }

int main(void) {
    AppState* state = app_state();
    state->autopilot_state = AP_STATE_AUTO;
    target_on_server(AP_STATE_AUTO, 359);
    target_on_adjust_sent(1);
    target_on_server(AP_STATE_AUTO, 359);
    assert(state->target_heading == 0 && timer.fire);
    target_on_server(AP_STATE_AUTO, 0);
    assert(!timer.fire && !target_on_failure());

    state->autopilot_state = AP_STATE_WIND;
    target_on_server(AP_STATE_WIND, 85);
    target_on_adjust_sent(-10);
    assert(state->target_wind == 75);
    assert(target_on_failure() && !strcmp(feedback, "FAILED"));
    assert(state->target_wind == 85 && buzzes == 1);

    target_on_adjust_sent(10);
    timer.fire(NULL);
    assert(!strcmp(feedback, "REVERTED") && state->target_wind == 85 && buzzes == 2);
    target_on_adjust_sent(10);
    target_on_server(AP_STATE_WIND, TARGET_NONE);
    assert(state->target_wind == TARGET_NONE && !target_on_failure());
    return 0;
}
