#include "state.h"

#include "protocol.h"

// Start disconnected with an unknown autopilot state until the phone reports in.
static AppState s_state = {
    .conn = CONN_DISCONNECTED,
    .autopilot_state = AP_STATE_UNKNOWN,
    .target_heading = TARGET_NONE,
    .target_wind = TARGET_NONE,
};

AppState* app_state(void) {
    return &s_state;
}
