/*
 * SignalK telemetry — phone side (task 4).
 *
 * Builds the stream subscription and parses incoming SignalK delta messages,
 * converting units into the integer-encoded AppMessage values defined in
 * src/pkjs/protocol.js, then queues them on the outbox for relay to the watch.
 *
 * SignalK units in: angles in RADIANS, speeds in m/s. We emit:
 *   HEADING / WIND_ANGLE: integer degrees 0..359
 *   SOG / WIND_SPEED:     tenths of a knot (knots * SPEED_SCALE)
 *   AUTOPILOT_STATE:      AP_STATE int
 */

var protocol = require("./protocol");
var outbox = require("./outbox");

var AP_STATE = protocol.AP_STATE;
var AP_STATE_FROM_STRING = protocol.AP_STATE_FROM_STRING;
var SPEED_SCALE = protocol.SPEED_SCALE;

var MS_TO_KNOTS = 1.943844;
var RAD_TO_DEG = 180 / Math.PI;

// SignalK paths we subscribe to, at 1 Hz.
var PATHS = [
  "navigation.headingMagnetic",
  "navigation.speedOverGround",
  "environment.wind.speedApparent",
  "environment.wind.angleApparent",
  "steering.autopilot.state",
];

// The subscription object to send once the stream is open.
function subscription() {
  var subs = [];
  for (var i = 0; i < PATHS.length; i++) {
    subs.push({ path: PATHS[i], period: 1000 });
  }
  return { context: "vessels.self", subscribe: subs };
}

// Normalize any angle in radians to an integer 0..359 degrees. Handles negative
// inputs (e.g. apparent wind angle reported as -PI..PI relative to the bow).
function radToDeg360(rad) {
  var deg = rad * RAD_TO_DEG;
  deg = deg % 360;
  if (deg < 0) {
    deg += 360;
  }
  return Math.round(deg) % 360;
}

// m/s -> tenths of a knot (integer).
function msToKnotTenths(ms) {
  return Math.round(ms * MS_TO_KNOTS * SPEED_SCALE);
}

function isNum(v) {
  return typeof v === "number" && isFinite(v);
}

// Map a single SignalK { path, value } pair to a telemetry key/value, queueing
// it on the outbox. Unknown paths and missing/null values are ignored.
function applyValue(path, value) {
  switch (path) {
    case "navigation.headingMagnetic":
      if (isNum(value)) {
        outbox.set("HEADING", radToDeg360(value));
      }
      break;
    case "environment.wind.angleApparent":
      if (isNum(value)) {
        outbox.set("WIND_ANGLE", radToDeg360(value));
      }
      break;
    case "navigation.speedOverGround":
      if (isNum(value)) {
        outbox.set("SOG", msToKnotTenths(value));
      }
      break;
    case "environment.wind.speedApparent":
      if (isNum(value)) {
        outbox.set("WIND_SPEED", msToKnotTenths(value));
      }
      break;
    case "steering.autopilot.state":
      if (typeof value === "string") {
        var ap = AP_STATE_FROM_STRING[value];
        if (ap === undefined) {
          ap = AP_STATE.UNKNOWN;
        }
        outbox.set("AUTOPILOT_STATE", ap);
      }
      break;
    default:
      break; // ignore unknown paths
  }
}

// Parse a SignalK delta message and relay telemetry. Defensive about shape.
function handleMessage(msg) {
  if (!msg || !msg.updates || !msg.updates.length) {
    return;
  }
  for (var i = 0; i < msg.updates.length; i++) {
    var update = msg.updates[i];
    var values = update && update.values;
    if (!values || !values.length) {
      continue;
    }
    for (var j = 0; j < values.length; j++) {
      var v = values[j];
      if (v && v.path !== undefined && v.value !== undefined && v.value !== null) {
        applyValue(v.path, v.value);
      }
    }
  }
}

module.exports = {
  subscription: subscription,
  handleMessage: handleMessage,
  PATHS: PATHS,
};
