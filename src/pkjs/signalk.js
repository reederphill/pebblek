/*
 * SignalK client — phone side.
 *
 * Maintains a WebSocket to the server's streaming endpoint, authenticating with
 * the configured access token (token-based auth). Reconnects automatically with
 * exponential backoff and reports connection-status changes via onStatus.
 * Incoming server messages are handed to onMessage (task 4 parses deltas there),
 * and onOpen fires once connected (task 4 sends the subscription there).
 *
 * Auth note: PebbleKit JS WebSocket follows browser semantics and cannot set an
 * Authorization header, so the token is passed as the `token` query parameter,
 * which signalk-server accepts for the stream endpoint.
 */

var protocol = require("./protocol");
var CONN = protocol.CONN;

var RECONNECT_MIN_MS = 2000;
var RECONNECT_MAX_MS = 30000;
var WS_OPEN = 1;

// Build ws(s)://host:port/signalk/v1/stream?subscribe=none&token=... from a
// user-entered server address (which may or may not include a scheme/path).
function buildStreamUrl(serverAddress, token) {
  var addr = ("" + serverAddress).trim();
  var secure = /^(wss|https):\/\//i.test(addr);
  addr = addr.replace(/^[a-z]+:\/\//i, ""); // drop any scheme
  addr = addr.replace(/[\/?#].*$/, "");      // drop any path/query/hash
  var url = (secure ? "wss" : "ws") + "://" + addr + "/signalk/v1/stream?subscribe=none";
  if (token) {
    url += "&token=" + encodeURIComponent(token);
  }
  return url;
}

function redactToken(url) {
  return url.replace(/token=[^&]+/, "token=***");
}

function SignalKClient(handlers) {
  handlers = handlers || {};
  this._onStatus = handlers.onStatus || function () {};
  this._onMessage = handlers.onMessage || function () {};
  this._onOpen = handlers.onOpen || function () {};

  this._serverAddress = null;
  this._token = null;
  this._ws = null;
  this._status = CONN.NO_CONFIG;
  this._reconnectMs = RECONNECT_MIN_MS;
  this._reconnectTimer = null;
  this._wantConnected = false;
}

SignalKClient.prototype.status = function () {
  return this._status;
};

SignalKClient.prototype._setStatus = function (status) {
  if (status === this._status) {
    return;
  }
  this._status = status;
  this._onStatus(status);
};

// Connect (or reconnect) to the given server using an already-obtained token.
// The access-request/token handling lives in auth.js; this client just needs a
// valid token. Call again to switch server or token.
SignalKClient.prototype.configure = function (serverAddress, token) {
  this._serverAddress = serverAddress;
  this._token = token;
  this._clearReconnectTimer();
  this._reconnectMs = RECONNECT_MIN_MS;
  this._connect();
};

SignalKClient.prototype.disconnect = function () {
  this._wantConnected = false;
  this._clearReconnectTimer();
  this._closeSocket();
  this._setStatus(CONN.NO_CONFIG);
};

// Send a JSON message to the server (used by task 4 for subscriptions). Returns
// false if the socket is not open.
SignalKClient.prototype.send = function (obj) {
  if (this._ws && this._ws.readyState === WS_OPEN) {
    this._ws.send(JSON.stringify(obj));
    return true;
  }
  return false;
};

SignalKClient.prototype._connect = function () {
  if (!this._serverAddress || !this._token) {
    this._wantConnected = false;
    this._closeSocket();
    this._setStatus(CONN.NO_CONFIG);
    return;
  }

  this._wantConnected = true;
  this._closeSocket();
  this._setStatus(CONN.CONNECTING);

  var url = buildStreamUrl(this._serverAddress, this._token);
  console.log("signalk: connecting to " + redactToken(url));

  var ws;
  try {
    ws = new WebSocket(url);
  } catch (e) {
    console.log("signalk: WebSocket construction failed: " + e);
    this._setStatus(CONN.DISCONNECTED);
    this._scheduleReconnect();
    return;
  }
  this._ws = ws;

  var self = this;

  ws.onopen = function () {
    console.log("signalk: connected");
    self._reconnectMs = RECONNECT_MIN_MS;
    self._setStatus(CONN.CONNECTED);
    self._onOpen(self);
  };

  ws.onmessage = function (evt) {
    var msg;
    try {
      msg = JSON.parse(evt.data);
    } catch (e) {
      return;
    }
    self._onMessage(msg, self);
  };

  ws.onerror = function () {
    // onclose follows and drives the reconnect; just log here.
    console.log("signalk: socket error");
  };

  ws.onclose = function () {
    self._ws = null;
    if (self._wantConnected) {
      self._setStatus(CONN.DISCONNECTED);
      self._scheduleReconnect();
    }
  };
};

SignalKClient.prototype._closeSocket = function () {
  var ws = this._ws;
  if (ws) {
    ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null;
    try {
      ws.close();
    } catch (e) {
      // ignore
    }
    this._ws = null;
  }
};

SignalKClient.prototype._scheduleReconnect = function () {
  if (!this._wantConnected) {
    return;
  }
  this._clearReconnectTimer();
  var delay = this._reconnectMs;
  console.log("signalk: reconnecting in " + Math.round(delay / 1000) + "s");
  var self = this;
  this._reconnectTimer = setTimeout(function () {
    self._reconnectTimer = null;
    self._connect();
  }, delay);
  this._reconnectMs = Math.min(this._reconnectMs * 2, RECONNECT_MAX_MS);
};

SignalKClient.prototype._clearReconnectTimer = function () {
  if (this._reconnectTimer) {
    clearTimeout(this._reconnectTimer);
    this._reconnectTimer = null;
  }
};

module.exports = SignalKClient;
// Exposed for unit-style checking of URL construction.
module.exports.buildStreamUrl = buildStreamUrl;
