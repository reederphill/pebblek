/*
 * SignalK access-request authentication (phone side).
 *
 * Spec: https://signalk.org/specification/1.5.0/doc/access_requests.html
 *   1. POST /signalk/v1/access/requests { clientId, description }
 *        -> 202 { state: "PENDING", href }
 *   2. Poll GET <href> until state === "COMPLETED":
 *        accessRequest.permission "APPROVED" -> persist accessRequest.token
 *        accessRequest.permission "DENIED"   -> report denial
 *   3. The token is then used for the stream (?token=) and REST PUTs (Bearer).
 *
 * The clientId is persistent (see settings.ensureClientId) so re-requests from
 * the same watch are recognised by the server.
 */

var settings = require("./settings");
var xhrJson = require("./http").xhrJson;

var POLL_INTERVAL_MS = 3000;
var POLL_TIMEOUT_MS = 5 * 60 * 1000; // stop waiting for approval after 5 minutes

// http(s)://host:port base derived from the user-entered server address.
function baseHttpUrl(serverAddress) {
  var addr = ("" + serverAddress).trim();
  var secure = /^(wss|https):\/\//i.test(addr);
  addr = addr.replace(/^[a-z]+:\/\//i, "").replace(/[\/?#].*$/, "");
  return (secure ? "https" : "http") + "://" + addr;
}

function AccessRequester() {
  this._timer = null;
  this._active = false;
  this._deadline = 0;
}

// Stop any in-flight request/polling.
AccessRequester.prototype.cancel = function () {
  this._active = false;
  if (this._timer) {
    clearTimeout(this._timer);
    this._timer = null;
  }
};

// Run the access-request flow.
// handlers: onPending(), onApproved(token), onDenied(), onError(message)
AccessRequester.prototype.request = function (serverAddress, handlers) {
  this.cancel();
  this._active = true;
  this._deadline = Date.now() + POLL_TIMEOUT_MS;

  var self = this;
  var base = baseHttpUrl(serverAddress);
  var clientId = settings.ensureClientId();
  var description = settings.loadConfig().description;
  // Request readwrite so the watch can issue autopilot PUTs; without this the
  // server grants read-only access and commands would be rejected.
  var body = JSON.stringify({
    clientId: clientId,
    description: description,
    permissions: "readwrite",
  });

  console.log("auth: requesting access (clientId=" + clientId + ")");
  xhrJson(
    "POST",
    base + "/signalk/v1/access/requests",
    { "Content-Type": "application/json" },
    body,
    function (err, status, data) {
      if (!self._active) {
        return;
      }
      if (err || (status !== 202 && status !== 200)) {
        handlers.onError("access request failed (" + (err || status) + ")");
        self.cancel();
        return;
      }
      var href = data && data.href;
      if (!href) {
        handlers.onError("access response missing href");
        self.cancel();
        return;
      }
      handlers.onPending();
      self._poll(base, href, handlers);
    }
  );
};

AccessRequester.prototype._poll = function (base, href, handlers) {
  var self = this;
  if (!self._active) {
    return;
  }
  if (Date.now() > self._deadline) {
    handlers.onError("approval timed out");
    self.cancel();
    return;
  }

  xhrJson("GET", base + href, null, null, function (err, status, data) {
    if (!self._active) {
      return;
    }
    if (err) {
      // Transient network error — keep polling.
      self._schedulePoll(base, href, handlers);
      return;
    }
    if (data && data.state === "COMPLETED") {
      var ar = data.accessRequest || {};
      if (ar.permission === "APPROVED" && ar.token) {
        settings.setToken(ar.token);
        self.cancel();
        console.log("auth: access APPROVED");
        handlers.onApproved(ar.token);
      } else {
        self.cancel();
        console.log("auth: access DENIED");
        handlers.onDenied();
      }
      return;
    }
    // PENDING (or anything else) — keep polling.
    self._schedulePoll(base, href, handlers);
  });
};

AccessRequester.prototype._schedulePoll = function (base, href, handlers) {
  var self = this;
  this._timer = setTimeout(function () {
    self._timer = null;
    self._poll(base, href, handlers);
  }, POLL_INTERVAL_MS);
};

// Check whether the stored token is still accepted by the server.
// cb(ok, status): ok=true on 200; status lets the caller spot 401/403 (revoked).
AccessRequester.prototype.validateToken = function (serverAddress, token, cb) {
  var base = baseHttpUrl(serverAddress);
  xhrJson(
    "GET",
    base + "/signalk/v1/api/vessels/self",
    { Authorization: "Bearer " + token },
    null,
    function (err, status) {
      cb(!err && status === 200, status);
    }
  );
};

module.exports = AccessRequester;
module.exports.baseHttpUrl = baseHttpUrl;
