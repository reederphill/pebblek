/*
 * pebblek watch <-> phone AppMessage protocol — the single source of truth for
 * the message contract between the phone (src/pkjs, PebbleKit JS) and the watch
 * (src/embeddedjs, Moddable XS).
 *
 * The message KEY NAMES below are declared in package.json -> pebble.messageKeys;
 * AppMessage references them by name. This module adds the VALUE encoding (units,
 * enums) that both sides must agree on.
 *
 * CommonJS so PebbleKit JS can `require('../common/protocol')`. The watch side
 * (ESM) keeps a mirror at src/embeddedjs/protocol.js — keep the values in sync.
 *
 * Encoding rules (AppMessage payloads are integers/strings only, so decimals are
 * scaled to integers):
 *
 *   Phone -> watch (telemetry):
 *     HEADING            int   degrees 0..359, magnetic
 *     AUTOPILOT_STATE    int   one of AP_STATE.*
 *     SOG                int   speed over ground, tenths of a knot (knots * 10)
 *     WIND_SPEED         int   apparent wind speed, tenths of a knot (knots * 10)
 *     WIND_ANGLE         int   degrees 0..359, apparent, relative to the bow (0 = ahead)
 *     CONNECTION_STATUS  int   one of CONN.*
 *
 *   Watch -> phone (commands):
 *     CMD_ADJUST_HEADING int   relative degrees: +1, +10, -1, -10
 *     CMD_TOGGLE_ENGAGE  int   1 = toggle autopilot between AUTO and STANDBY
 *
 *   Phone -> watch (command acknowledgement):
 *     CMD_RESULT         int   one of CMD_RESULT.*
 */

// Autopilot state, mirrors signalk-autopilot's steering.autopilot.state strings.
var AP_STATE = {
  STANDBY: 0,
  AUTO: 1,
  WIND: 2,
  ROUTE: 3,
  UNKNOWN: 255,
};

// Map between the SignalK string state and our compact int enum.
var AP_STATE_TO_STRING = {
  0: "standby",
  1: "auto",
  2: "wind",
  3: "route",
  255: "unknown",
};

var AP_STATE_FROM_STRING = {
  standby: AP_STATE.STANDBY,
  auto: AP_STATE.AUTO,
  wind: AP_STATE.WIND,
  route: AP_STATE.ROUTE,
};

// Connection status reported by the phone so the watch can show a clear state.
var CONN = {
  NO_CONFIG: 0,   // no server address / token configured yet
  DISCONNECTED: 1,
  CONNECTING: 2,
  CONNECTED: 3,
};

// Result of an autopilot command, echoed back so the watch can clear its pending UI.
var CMD_RESULT = {
  OK: 0,
  FAILED: 1,
};

// Scaling factor for speeds sent over AppMessage (tenths of a knot).
var SPEED_SCALE = 10;

module.exports = {
  AP_STATE: AP_STATE,
  AP_STATE_TO_STRING: AP_STATE_TO_STRING,
  AP_STATE_FROM_STRING: AP_STATE_FROM_STRING,
  CONN: CONN,
  CMD_RESULT: CMD_RESULT,
  SPEED_SCALE: SPEED_SCALE,
};
