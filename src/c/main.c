/*
 * pebblek — native C watch app (SignalK boat monitor + autopilot remote).
 *
 * The phone side (src/pkjs) is unchanged PebbleKit JS: it talks to the SignalK
 * server, relays telemetry to the watch over AppMessage, and turns our button
 * commands into autopilot REST calls. This file only wires up the on-watch
 * pieces; each lives in its own module:
 *   - state   — the shared AppState the dashboard renders
 *   - ui      — window, dashboard rendering, transient feedback
 *   - comm    — AppMessage in (telemetry/acks) and out (commands)
 *   - buttons — side-button -> autopilot command mapping
 *
 * The AppMessage value encoding is shared with the phone via protocol.h /
 * src/common/protocol.js.
 */

#include <pebble.h>

#include "comm.h"
#include "ui.h"

static void init(void) {
    ui_init();
    comm_init();
}

static void deinit(void) {
    ui_deinit();
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
