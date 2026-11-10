#include <assert.h>
#include "state.h"
#include "protocol.h"

static time_t now = 1800000000;
time_t time(time_t* out) { if (out) *out = now; return now; }

int main(void) {
    AppState* state = app_state();
    assert(state->sog == DATA_NONE);
    assert(!state_fresh(FIELD_SOG));
    state->conn = CONN_CONNECTED;
    state->sog = 0;
    state->updated[FIELD_SOG] = now;
    state->updated[FIELD_WIND_SPEED] = now + 2;
    assert(state_fresh(FIELD_SOG));
    assert(state_next_expiry() == now + 10);
    now += 9;
    assert(state_fresh(FIELD_SOG));
    now++;
    assert(!state_fresh(FIELD_SOG));
    assert(state_fresh(FIELD_WIND_SPEED));
    assert(state_next_expiry() == now + 2);
    now += 2;
    assert(!state_fresh(FIELD_WIND_SPEED));
    assert(state_next_expiry() == 0); // Phone silent: no indefinitely live readings.
    state->updated[FIELD_SOG] = now;
    assert(state_fresh(FIELD_SOG));
    state->conn = CONN_DISCONNECTED;
    assert(!state_fresh(FIELD_SOG));
    state_invalidate();
    state->conn = CONN_CONNECTED;
    assert(!state_fresh(FIELD_SOG)); // Connection alone does not revive data.
    state->updated[FIELD_SOG] = now + 6;
    assert(!state_fresh(FIELD_SOG));
    return 0;
}
