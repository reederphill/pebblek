/*
 * Autopilot commands — watch -> phone -> signalk-autopilot plugin REST PUTs.
 *
 * The watch sends AppMessages carrying CMD_ADJUST_HEADING (relative degrees) and
 * /or CMD_TOGGLE_ENGAGE (1). We translate those into REST PUTs against the
 * signalk-autopilot plugin and echo a CMD_RESULT (OK/FAILED) back to the watch.
 *
 * Endpoints (base = baseHttpUrl(serverAddress)):
 *   adjustHeading:
 *     PUT base + "/signalk/v1/api/vessels/self/steering/autopilot/actions/adjustHeading"
 *     body { "value": <delta> }
 *   state (engage/disengage):
 *     PUT base + "/signalk/v1/api/vessels/self/steering/autopilot/state"
 *     body { "value": "auto" | "standby" }
 *
 * Auth: every PUT carries Authorization: "Bearer " + token and
 * Content-Type: application/json. HTTP 200/202 -> CMD_RESULT.OK, anything else
 * (incl. 401/403/network) -> CMD_RESULT.FAILED.
 */

var settings = require("./settings");
var state = require("./state");
var outbox = require("./outbox");
var xhrJson = require("./http").xhrJson;
var baseHttpUrl = require("./auth").baseHttpUrl;
var protocol = require("./protocol");

var AP_STATE = protocol.AP_STATE;
var AP_STATE_TO_STRING = protocol.AP_STATE_TO_STRING;
var CMD_RESULT = protocol.CMD_RESULT;

function sendResult(ok) {
  outbox.set("CMD_RESULT", ok ? CMD_RESULT.OK : CMD_RESULT.FAILED);
}

// Common PUT wrapper: builds the URL/headers and maps the response to a result.
function put(path, bodyObj, label) {
  var cfg = settings.loadConfig();
  var token = settings.loadAuth().token;
  if (!cfg.serverAddress || !token) {
    console.log("autopilot: " + label + " skipped — no server/token");
    sendResult(false);
    return;
  }

  var url = baseHttpUrl(cfg.serverAddress) +
    "/signalk/v1/api/vessels/self/steering/autopilot/" + path;
  var headers = {
    Authorization: "Bearer " + token,
    "Content-Type": "application/json",
  };

  xhrJson("PUT", url, headers, JSON.stringify(bodyObj), function (err, status) {
    var ok = !err && (status === 200 || status === 202);
    console.log("autopilot: " + label + " -> " + (err || status) + (ok ? " OK" : " FAILED"));
    sendResult(ok);
  });
}

// Nudge the target heading by a relative number of degrees (+1/+10/-1/-10).
//
// TODO(hardware): verify whether the signalk-autopilot plugin expects the
// adjustHeading value in DEGREES or RADIANS. The plugin README implies degrees,
// so we send degrees for now; confirm on real hardware and convert if needed.
function adjustHeading(delta) {
  put("actions/adjustHeading", { value: delta }, "adjustHeading(" + delta + ")");
}

// Toggle the autopilot between engaged (auto) and disengaged (standby), based on
// the latest known state streamed in via telemetry.
function toggleEngage() {
  var current = state.getAutopilotState();
  var next = current === AP_STATE.AUTO ? AP_STATE.STANDBY : AP_STATE.AUTO;
  var value = AP_STATE_TO_STRING[next];
  put("state", { value: value }, "toggleEngage(" + value + ")");
}

// Handle a raw AppMessage payload from the watch. A single message may carry
// either command (or, defensively, both).
function handleCommand(payload) {
  if (!payload) {
    return;
  }
  if (payload.CMD_ADJUST_HEADING !== undefined && payload.CMD_ADJUST_HEADING !== null) {
    adjustHeading(payload.CMD_ADJUST_HEADING | 0);
  }
  if (payload.CMD_TOGGLE_ENGAGE) {
    toggleEngage();
  }
}

module.exports = {
  handleCommand: handleCommand,
  adjustHeading: adjustHeading,
  toggleEngage: toggleEngage,
};
