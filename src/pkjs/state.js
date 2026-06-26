/*
 * Shared phone-side runtime state.
 *
 * Currently just the latest known autopilot state (an AP_STATE int from
 * protocol.js). Telemetry parsing (index.js) writes it as it streams in; the
 * toggle-engage command (autopilot.js) reads it to decide which state to PUT.
 * Kept in its own module so neither side has to require the other.
 */

var protocol = require("./protocol");

var autopilotState = protocol.AP_STATE.UNKNOWN;

module.exports = {
  getAutopilotState: function () {
    return autopilotState;
  },
  setAutopilotState: function (s) {
    autopilotState = s;
  },
};
