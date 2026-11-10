/* SignalK measurement timestamps travel with each reading so cached deltas and
 * a stalled phone cannot leave live-looking instruments on the watch. */
var protocol = require("./protocol");
var outbox = require("./outbox");
var MS_TO_KNOTS = 1.943844;
var RAD_TO_DEG = 180 / Math.PI;

function angle(rad) {
  return Math.round(((rad * RAD_TO_DEG) % 360 + 360) % 360) % 360;
}
function speed(ms) {
  return ms >= 0 && ms < 1000 ? Math.round(ms * MS_TO_KNOTS * protocol.SPEED_SCALE) : -1;
}
function apState(value) {
  var state = protocol.AP_STATE_FROM_STRING[value];
  return typeof state === "number" ? state : protocol.AP_STATE.UNKNOWN;
}
var FIELDS = {
  "navigation.headingMagnetic": ["HEADING", angle],
  "navigation.speedOverGround": ["SOG", speed],
  "navigation.speedThroughWater": ["STW", speed],
  "environment.wind.speedApparent": ["WIND_SPEED", speed],
  "environment.wind.angleApparent": ["WIND_ANGLE", angle],
  "environment.wind.speedTrue": ["TRUE_WIND_SPEED", speed],
  "environment.wind.angleTrueWater": ["TRUE_WIND_ANGLE", angle],
  "steering.autopilot.state": ["AUTOPILOT_STATE", apState],
  "steering.autopilot.target.headingMagnetic": ["TARGET_HEADING", angle],
  "steering.autopilot.target.windAngleApparent": ["TARGET_WIND_ANGLE", angle],
};
var PATHS = Object.keys(FIELDS);
var latest = {};

function subscription() {
  return { context: "vessels.self", subscribe: PATHS.map(function (path) {
    return { path: path, period: 1000 };
  }) };
}

function unavailable(field) {
  outbox.set(field[0], field[0] === "AUTOPILOT_STATE" ? protocol.AP_STATE.UNKNOWN : protocol.DATA_NONE);
  outbox.set(field[0] + "_TIME", 0);
}

// On a connection/server change, remove pending readings as well as timestamps.
function reset() {
  latest = {};
  PATHS.forEach(function (path) { unavailable(FIELDS[path]); });
}

function handleMessage(msg) {
  if (!msg || !Array.isArray(msg.updates)) return;
  var now = Date.now();
  msg.updates.forEach(function (update) {
    if (!update || !Array.isArray(update.values)) return;
    // Signal K servers timestamp deltas. Missing/malformed timestamps cannot
    // establish freshness; future timestamps beyond small clock skew cannot either.
    var stamp = typeof update.timestamp === "string" ? Date.parse(update.timestamp) : NaN;
    update.values.forEach(function (v) {
      if (!v || !Object.prototype.hasOwnProperty.call(FIELDS, v.path)) return;
      var field = FIELDS[v.path];
      if (!isFinite(stamp) || stamp > now + 5000) {
        unavailable(field);
        return;
      }
      if (latest[v.path] !== undefined && stamp < latest[v.path]) return;
      latest[v.path] = stamp;
      var isState = field[0] === "AUTOPILOT_STATE";
      var valid = isState ? typeof v.value === "string" :
        typeof v.value === "number" && isFinite(v.value);
      var value = valid ? field[1](v.value) : protocol.DATA_NONE;
      if (!valid || value < 0 || (isState && value === protocol.AP_STATE.UNKNOWN) ||
          now - stamp >= protocol.TELEMETRY_STALE_SECONDS * 1000) {
        unavailable(field);
        return;
      }
      outbox.set(field[0], value);
      outbox.set(field[0] + "_TIME", Math.floor(stamp / 1000));
    });
  });
}

module.exports = { subscription: subscription, handleMessage: handleMessage, reset: reset, PATHS: PATHS };
