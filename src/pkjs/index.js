/*
 * PebbleKit JS — runs on the phone. Responsibilities:
 *   - present the settings page (SignalK server address + device name)
 *   - obtain a token via the SignalK access-request flow (see auth.js)
 *   - maintain the SignalK stream connection and relay data to the watch
 *   - (task 5) translate watch button commands into autopilot REST calls
 */

var Clay = require("@rebble/clay");
var clayConfig = require("./config");
var settings = require("./settings");
var SignalKClient = require("./signalk");
var AccessRequester = require("./auth");
var protocol = require("./protocol");
var outbox = require("./outbox");
var telemetry = require("./telemetry");
var autopilot = require("./autopilot");

var CONN = protocol.CONN;
var AUTH_RETRY_MS = 15000; // retry a failed access request after a network error

// We handle the config events ourselves: the server address stays on the phone,
// so Clay's automatic AppMessage send to the watch is disabled.
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var requester = new AccessRequester();
var authRetryTimer = null;
var lastStatus = null;

// Send a dictionary to the watch. Routed through the outbox so it respects the
// one-AppMessage-in-flight rule and is coalesced with telemetry. messageKeys are
// declared in package.json and referenced by name.
function sendToWatch(dict) {
  outbox.setAll(dict);
}

// Report connection status to the watch, de-duplicated across the auth and
// stream phases.
function reportStatus(status) {
  if (status === lastStatus) {
    return;
  }
  lastStatus = status;
  console.log("pebblek: status=" + status);
  sendToWatch({ CONNECTION_STATUS: status });
}

var client = new SignalKClient({
  onStatus: reportStatus,
  onOpen: function (c) {
    // task 4: subscribe to the telemetry paths once the stream is open.
    c.send(telemetry.subscription());
  },
  onMessage: function (msg) {
    // task 4: parse deltas and relay telemetry to the watch via the outbox.
    telemetry.handleMessage(msg);
  },
});

// task 5: translate watch button commands into autopilot REST PUTs.
Pebble.addEventListener("appmessage", function (e) {
  autopilot.handleCommand(e && e.payload);
});

function clearAuthRetry() {
  if (authRetryTimer) {
    clearTimeout(authRetryTimer);
    authRetryTimer = null;
  }
}

// Entry point for (re)establishing everything based on current settings.
function start() {
  clearAuthRetry();
  requester.cancel();

  var cfg = settings.loadConfig();
  if (!cfg.serverAddress) {
    client.disconnect();
    reportStatus(CONN.NO_CONFIG);
    return;
  }

  if (settings.hasToken()) {
    connectStream();
  } else {
    obtainAccess();
  }
}

// Run the access-request flow, then connect once approved.
function obtainAccess() {
  var cfg = settings.loadConfig();
  reportStatus(CONN.CONNECTING);
  requester.request(cfg.serverAddress, {
    onPending: function () {
      reportStatus(CONN.AWAITING_APPROVAL);
    },
    onApproved: function (/* token */) {
      connectStream();
    },
    onDenied: function () {
      reportStatus(CONN.ACCESS_DENIED);
    },
    onError: function (msg) {
      console.log("auth: " + msg + " — retrying in " + AUTH_RETRY_MS / 1000 + "s");
      reportStatus(CONN.DISCONNECTED);
      clearAuthRetry();
      authRetryTimer = setTimeout(obtainAccess, AUTH_RETRY_MS);
    },
  });
}

// Validate the stored token, then open the stream. A revoked token (401/403)
// triggers a fresh access request.
function connectStream() {
  var cfg = settings.loadConfig();
  var token = settings.loadAuth().token;
  reportStatus(CONN.CONNECTING);

  requester.validateToken(cfg.serverAddress, token, function (ok, status) {
    if (ok) {
      client.configure(cfg.serverAddress, token);
    } else if (status === 401 || status === 403) {
      console.log("auth: token rejected (" + status + ") — re-requesting access");
      settings.clearToken();
      obtainAccess();
    } else {
      // Network/server issue: try the stream anyway; the client will retry.
      client.configure(cfg.serverAddress, token);
    }
  });
}

Pebble.addEventListener("ready", function () {
  var cfg = settings.loadConfig();
  console.log(
    "pebblek pkjs ready; server=" + (cfg.serverAddress || "(none)") +
    " hasToken=" + settings.hasToken()
  );
  start();
});

Pebble.addEventListener("showConfiguration", function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener("webviewclosed", function (e) {
  // No response means the user backed out without saving — keep existing settings.
  if (!e || !e.response) {
    return;
  }

  var previousServer = settings.loadConfig().serverAddress;
  var dict = clay.getSettings(e.response, false);
  settings.saveConfig({ serverAddress: dict.serverAddress, description: dict.description });

  var cfg = settings.loadConfig();
  // A different server invalidates any token we hold for the old one.
  if (cfg.serverAddress !== previousServer) {
    settings.clearToken();
  }
  console.log("pebblek settings saved; server=" + cfg.serverAddress);
  start();
});
