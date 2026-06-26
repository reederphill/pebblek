/*
 * AppMessage outbox — phone -> watch.
 *
 * Pebble allows only ONE AppMessage in flight at a time: the next send must wait
 * for the success or failure (nack) callback of the previous one. This module
 * enforces that, while coalescing rapidly-changing telemetry so the watch is not
 * flooded.
 *
 * Design:
 *   - set(key, value): record the latest value for a messageKey. Equal values are
 *     dropped so unchanged telemetry never costs a send.
 *   - Pending keys are flushed at most once per FLUSH_INTERVAL_MS, batched into a
 *     SINGLE sendAppMessage dict. While a send is in flight nothing else is sent.
 *   - On nack the just-sent keys are merged back into the pending set (unless a
 *     newer value arrived meanwhile) and retried on the next flush tick.
 *
 * Telemetry is coalesced (only the latest value matters); command results
 * (CMD_RESULT) are also routed through here so they obey the one-in-flight rule.
 */

var FLUSH_INTERVAL_MS = 1000;

function Outbox() {
  this._pending = {};      // key -> latest value waiting to be sent
  this._inFlight = false;  // a sendAppMessage is awaiting its callback
  this._timer = null;      // scheduled flush
}

// Queue the latest value for a messageKey. Unchanged values are ignored.
Outbox.prototype.set = function (key, value) {
  if (this._pending[key] === value) {
    return;
  }
  this._pending[key] = value;
  this._schedule();
};

// Queue several keys at once (object of key -> value).
Outbox.prototype.setAll = function (dict) {
  for (var k in dict) {
    if (dict.hasOwnProperty(k)) {
      this.set(k, dict[k]);
    }
  }
};

Outbox.prototype._schedule = function () {
  if (this._inFlight || this._timer) {
    return;
  }
  var self = this;
  this._timer = setTimeout(function () {
    self._timer = null;
    self._flush();
  }, FLUSH_INTERVAL_MS);
};

Outbox.prototype._flush = function () {
  if (this._inFlight) {
    return;
  }
  var dict = this._pending;
  var keys = Object.keys(dict);
  if (keys.length === 0) {
    return;
  }
  // Take the current batch; new values that arrive during the send accumulate
  // into a fresh pending object.
  this._pending = {};
  this._inFlight = true;

  var self = this;
  Pebble.sendAppMessage(
    dict,
    function () {
      self._inFlight = false;
      // Drain anything queued while we were sending.
      if (Object.keys(self._pending).length > 0) {
        self._schedule();
      }
    },
    function (e) {
      console.log("outbox: send failed (" + (e && e.error ? e.error : e) + ") — retrying");
      self._inFlight = false;
      // Requeue the failed keys, but never clobber a newer value.
      for (var k in dict) {
        if (dict.hasOwnProperty(k) && !self._pending.hasOwnProperty(k)) {
          self._pending[k] = dict[k];
        }
      }
      self._schedule();
    }
  );
};

module.exports = new Outbox();
