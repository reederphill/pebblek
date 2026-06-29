#include "buttons.h"

#include "comm.h"
#include "protocol.h"
#include "state.h"
#include "ui.h"

static void up_click(ClickRecognizerRef r, void* c) {
    comm_send_int(MESSAGE_KEY_CMD_ADJUST_HEADING, 1);
    ui_set_feedback("+1°");
}
static void up_long(ClickRecognizerRef r, void* c) {
    comm_send_int(MESSAGE_KEY_CMD_ADJUST_HEADING, 10);
    ui_set_feedback("+10°");
}
static void down_click(ClickRecognizerRef r, void* c) {
    comm_send_int(MESSAGE_KEY_CMD_ADJUST_HEADING, -1);
    ui_set_feedback("-1°");
}
static void down_long(ClickRecognizerRef r, void* c) {
    comm_send_int(MESSAGE_KEY_CMD_ADJUST_HEADING, -10);
    ui_set_feedback("-10°");
}
// SELECT engages/disengages the autopilot. When already engaged (any mode), a
// press disengages. When in standby, the chosen mode is engaged: a single click
// engages AUTO, a long press engages WIND.
static void engage(ApState mode_when_standby, const char* engage_label) {
    const int ap = app_state()->autopilot_state;
    const bool engaged = (ap != AP_STATE_STANDBY && ap != AP_STATE_UNKNOWN);
    const ApState target = engaged ? AP_STATE_STANDBY : mode_when_standby;
    comm_send_int(MESSAGE_KEY_CMD_SET_STATE, target);
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
    window_long_click_subscribe(BUTTON_ID_UP, 0, up_long, NULL);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
    window_long_click_subscribe(BUTTON_ID_DOWN, 0, down_long, NULL);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
    window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long, NULL);
}
