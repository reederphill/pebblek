/*
 * AppMessage transport: receiving telemetry/acks from the phone and sending
 * autopilot commands to it. Inbound messages update app_state() and trigger UI
 * updates (redraw + feedback); outbound messages carry button commands.
 *
 * The value encoding is the watch <-> phone contract in protocol.h.
 */

#pragma once

#include <pebble.h>

// Register AppMessage handlers and open the inbox/outbox. Call once at startup.
void comm_init(void);
void comm_deinit(void);
// Drop commands that have not yet been submitted to AppMessage.
void comm_cancel_pending(void);

// Queue a relative heading adjustment (degrees). Adjustments queued while a
// message is in flight are summed and sent as one CMD_ADJUST_HEADING, so a
// held button can step quickly without dropping presses. The AUTO heading target
// / WIND angle target held in app_state() is updated immediately, and target
// telemetry that disagrees with it is ignored briefly, so stale updates already in
// flight from the phone can't flash the old target back.
void comm_adjust_heading(int delta);

// Queue an autopilot state change (target ApState).
void comm_set_state(int ap_state);
