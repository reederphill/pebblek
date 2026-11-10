#include "state.h"

#include "protocol.h"

// Start disconnected with an unknown autopilot state until the phone reports in.
static AppState s_state = {
    .heading = DATA_NONE,
    .sog = DATA_NONE,
    .stw = DATA_NONE,
    .wind_speed = DATA_NONE,
    .wind_angle = DATA_NONE,
    .true_wind_speed = DATA_NONE,
    .true_wind_angle = DATA_NONE,
    .conn = CONN_DISCONNECTED,
    .autopilot_state = AP_STATE_UNKNOWN,
    .target_heading = TARGET_NONE,
    .target_wind = TARGET_NONE,
};

AppState* app_state(void) {
    return &s_state;
}

// Measurement time, not receipt time: cached/replayed packets cannot revive data.
bool state_fresh(TelemetryField field) {
    const time_t stamp = s_state.updated[field];
    const time_t now = time(NULL);
    return s_state.conn == CONN_CONNECTED && stamp > 0 &&
           stamp <= now + 5 && now < stamp + TELEMETRY_STALE_SECONDS;
}

time_t state_next_expiry(void) {
    time_t next = 0;
    for (int i = 0; i < FIELD_COUNT; i++) {
        if (state_fresh(i)) {
            const time_t expiry = s_state.updated[i] + TELEMETRY_STALE_SECONDS;
            if (!next || expiry < next) next = expiry;
        }
    }
    return next;
}

void state_invalidate(void) {
    memset(s_state.updated, 0, sizeof(s_state.updated));
}
