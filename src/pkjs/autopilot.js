/*
 * Autopilot commands — watch -> phone -> signalk-autopilot plugin REST PUTs.
 *
 * The watch sends AppMessages carrying CMD_ADJUST_HEADING (relative degrees)
 * and/or CMD_SET_STATE (a target AP_STATE). We translate those into REST PUTs
 * against the signalk-autopilot plugin and echo a CMD_RESULT (OK/FAILED) back to
 * the watch. The watch already knows the current state from telemetry, so it
 * decides the target state (engage AUTO/WIND or disengage to STANDBY).
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
var outbox = require("./outbox");
var xhrJson = require("./http").xhrJson;
var baseHttpUrl = require("./auth").baseHttpUrl;
var protocol = require("./protocol");

var AP_STATE_TO_STRING = protocol.AP_STATE_TO_STRING;
var CMD_RESULT = protocol.CMD_RESULT;

// Results are coalesced in the outbox, so several commands answered within one
// flush send a single CMD_RESULT. A failure must not be hidden by a later
// success: the watch reverts its optimistic target on FAILED.
function sendResult(ok) {
  if (ok && outbox.peek("CMD_RESULT") === CMD_RESULT.FAILED) {
    return;
  }
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

// Nudge the target heading by a relative number of degrees (+1/-1, or a
// multiple of 10 when the watch coalesces a held button's repeated steps).
//
// TODO(hardware): verify whether the signalk-autopilot plugin expects the
// adjustHeading value in DEGREES or RADIANS. The plugin README implies degrees,
// so we send degrees for now; confirm on real hardware and convert if needed.
function adjustHeading(delta) {
  put("actions/adjustHeading", { value: delta }, "adjustHeading(" + delta + ")");
}

// Switch the autopilot to the target state requested by the watch (AUTO/WIND to
// engage, STANDBY to disengage).
function setState(target) {
  var value = AP_STATE_TO_STRING[target];
  put("state", { value: value }, "setState(" + value + ")");
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
  if (payload.CMD_SET_STATE !== undefined && payload.CMD_SET_STATE !== null) {
    setState(payload.CMD_SET_STATE | 0);
  }
}

module.exports = {
  handleCommand: handleCommand,
  adjustHeading: adjustHeading,
  setState: setState,
};
