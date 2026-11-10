// Signal K notification paths identify active alarm conditions. Keep severity
// history for an activation until it clears, independently of telemetry expiry.
var http = require("./http");
var baseHttpUrl = require("./auth").baseHttpUrl;
var LEVEL = { normal: 0, nominal: 0, alert: 1, warn: 2, alarm: 3, emergency: 4 };
var PREFIX = "notifications.";
var RETRY_MS = 15000;

function has(obj, key) {
  return Object.prototype.hasOwnProperty.call(obj, key);
}

// The watch has room for 110 UTF-8 bytes and a NUL terminator.
function displayText(path, message) {
  var text = typeof message === "string" ? message.replace(/[\x00-\x1f\x7f]+/g, " ").trim() : "";
  if (!text) text = path.slice(PREFIX.length);
  var glyphs = text.match(/[\uD800-\uDBFF][\uDC00-\uDFFF]|[\s\S]/g) || [];
  var result = "", bytes = 0;
  for (var i = 0; i < glyphs.length; i++) {
    var glyph = glyphs[i], code = glyph.charCodeAt(0);
    if (glyph.length === 1 && code >= 0xd800 && code <= 0xdfff) glyph = "?";
    var size = glyph.length === 2 ? 4 : glyph === "?" || code < 128 ? 1 : code < 2048 ? 2 : 3;
    if (bytes + size > 110) break;
    result += glyph;
    bytes += size;
  }
  return result;
}

function walkSnapshot(node, path, visit) {
  if (!node || typeof node !== "object" || Array.isArray(node)) return;
  if (has(node, "value") && (node.value === null || (node.value && has(node.value, "state")))) {
    visit(path, node.value, node.timestamp);
    return;
  }
  Object.keys(node).forEach(function (key) {
    if (key !== "meta" && key !== "values" && key[0] !== "$") {
      walkSnapshot(node[key], path + "." + key, visit);
    }
  });
}

function Alerts(send) {
  var records = Object.create(null); // includes clear timestamps to reject late replay
  var nextId = 0, sequence = 0;
  var address = null, firstSync = true, synchronized = false;
  var openedAt = null, selfContext = null;
  var request = null, timer = null;

  function apply(path, value, timestamp, announce) {
    if (path.indexOf(PREFIX) !== 0 || path.length === PREFIX.length) return false;
    var level = 0;
    if (value !== null) {
      if (!value || typeof value.state !== "string" || !has(LEVEL, value.state)) return false;
      level = LEVEL[value.state];
    }
    var record = records[path];
    var stamp = typeof timestamp === "string" ? Date.parse(timestamp) : NaN;
    if (record && isFinite(stamp) && stamp < record.stamp) return false;
    if (!record) {
      record = records[path] = { id: ++nextId, level: 0, peak: 0, text: "", stamp: -Infinity };
    }
    if (isFinite(stamp)) record.stamp = stamp;
    var text = level ? displayText(path, value.message) : "";
    if (level !== record.level || text !== record.text) {
      var notify = announce && level > record.peak;
      record.level = level;
      record.peak = level ? Math.max(record.peak, level) : 0;
      record.text = text;
      send({ ALERT_ID: record.id, ALERT_LEVEL: level, ALERT_TEXT: text,
             ALERT_NOTIFY: notify ? ++sequence : 0 });
    }
    return true;
  }

  function cancelRequest() {
    clearTimeout(timer);
    timer = null;
    request = null;
  }

  function snapshot(token) {
    // Mark paths as they arrive on the stream while this particular GET is
    // outstanding. In particular, a late snapshot must not resurrect a clear.
    var touched = request = Object.create(null);
    function retry() {
      if (request !== touched) return;
      cancelRequest();
      timer = setTimeout(function () { snapshot(token); }, RETRY_MS);
    }
    timer = setTimeout(retry, RETRY_MS);
    http.xhrJson("GET", baseHttpUrl(address) + "/signalk/v1/api/vessels/self/notifications",
      { Authorization: "Bearer " + token }, null, function (err, status, tree) {
        if (request !== touched) return;
        // The notifications branch can be absent on a boat with no alarms.
        if (err || (status !== 404 &&
            (status !== 200 || !tree || typeof tree !== "object" || Array.isArray(tree)))) {
          retry();
          return;
        }
        cancelRequest();
        var present = Object.create(null);
        if (status === 200) walkSnapshot(tree, "notifications", function (path, value, stamp) {
          present[path] = true;
          // A concurrent delta wins ties and timestamps of unknown order.
          if (!has(touched, path) || Date.parse(stamp) > touched[path]) {
            var created = Date.parse(stamp);
            apply(path, value, stamp, !firstSync || created >= openedAt);
          }
        });
        Object.keys(records).forEach(function (path) {
          if (records[path].level && !present[path] && !has(touched, path)) apply(path, null, null, false);
        });
        firstSync = false;
        synchronized = true;
        send({ ALERT_SYNC: 1 });
      });
  }

  this.close = function () {
    cancelRequest();
    if (openedAt !== null) send({ ALERT_SYNC: 0 });
    openedAt = null;
    selfContext = null;
    synchronized = false;
  };

  this.setServer = function (server) {
    if (server === address) return;
    this.close();
    address = server;
    records = Object.create(null);
    firstSync = true;
    send({ ALERT_SYNC: 2 });
  };

  this.replay = function () {
    send({ ALERT_SYNC: 2 });
    Object.keys(records).forEach(function (path) {
      var r = records[path];
      if (r.level) send({ ALERT_ID: r.id, ALERT_LEVEL: r.level, ALERT_TEXT: r.text, ALERT_NOTIFY: 0 });
    });
    send({ ALERT_SYNC: synchronized ? 1 : 0 });
  };

  this.open = function (token) {
    openedAt = Date.now();
    send({ ALERT_SYNC: 0 });
    snapshot(token);
  };

  this.handleMessage = function (msg) {
    if (openedAt === null || !msg) return;
    // The Signal K welcome identifies the boat's full context for subsequent
    // deltas; contextless deltas on the self-only subscription also belong to it.
    if (typeof msg.self === "string") {
      selfContext = msg.self.indexOf("vessels.") === 0 ? msg.self : "vessels." + msg.self;
    }
    if (!Array.isArray(msg.updates) ||
        (msg.context && msg.context !== "vessels.self" && msg.context !== selfContext)) return;
    msg.updates.forEach(function (update) {
      if (!update || !Array.isArray(update.values)) return;
      update.values.forEach(function (entry) {
        if (!entry || typeof entry.path !== "string" || entry.path.indexOf(PREFIX) !== 0) return;
        // First-open replay can arrive on either transport. Treat historical
        // initial values alike; live deltas and reconnect escalations announce.
        var stamp = Date.parse(update.timestamp);
        var announce = !firstSync || !isFinite(stamp) || stamp >= openedAt;
        if (apply(entry.path, entry.value, update.timestamp, announce) && request) {
          request[entry.path] = stamp;
        }
      });
    });
  };
}

Alerts.subscription = { path: "notifications.*", policy: "instant", minPeriod: 0 };
module.exports = Alerts;
