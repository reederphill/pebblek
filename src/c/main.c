/*
 * pebblek — native C watch app (SignalK boat monitor + autopilot remote).
 *
 * The phone side (src/pkjs) is unchanged PebbleKit JS: it talks to the SignalK
 * server, relays telemetry to the watch over AppMessage, and turns our button
 * commands into autopilot REST calls. This file only wires up the on-watch
 * pieces; each lives in its own module:
 *   - state   — the shared AppState the dashboard renders
 *   - ui      — window, dashboard rendering
 *   - comm    — AppMessage in (telemetry/acks) and out (commands)
 *   - buttons — side-button -> autopilot command mapping
 *   - dial    — touch drag to dial in a larger change, SELECT to send
 *
 * The AppMessage value encoding is shared with the phone via protocol.h /
 * src/common/protocol.js.
 */

#include <pebble.h>

#include "comm.h"
#include "buttons.h"
#include "dial.h"
#include "ui.h"

static void init(void) {
    ui_init();
    comm_init();
    dial_init();
}

static void deinit(void) {
    buttons_cancel();
    dial_deinit();
    comm_deinit();
    ui_deinit();
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
