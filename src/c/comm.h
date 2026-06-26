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

// Send a single-integer command to the phone (e.g. MESSAGE_KEY_CMD_*).
void comm_send_int(uint32_t key, int value);
