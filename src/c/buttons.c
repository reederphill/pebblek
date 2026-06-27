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
static void select_long(ClickRecognizerRef r, void* c) {
    comm_send_int(MESSAGE_KEY_CMD_TOGGLE_ENGAGE, 1);
    const int ap = app_state()->autopilot_state;
    const bool engaged = (ap != AP_STATE_STANDBY && ap != AP_STATE_UNKNOWN);
    ui_set_feedback(engaged ? "DISENGAGE" : "ENGAGE");
}

void buttons_click_config_provider(void* context) {
    window_single_click_subscribe(BUTTON_ID_UP, up_click);
    window_long_click_subscribe(BUTTON_ID_UP, 0, up_long, NULL);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
    window_long_click_subscribe(BUTTON_ID_DOWN, 0, down_long, NULL);
    window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long, NULL);
}
