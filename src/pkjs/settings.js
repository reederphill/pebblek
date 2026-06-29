/*
 * Persisted phone-side state, split into two concerns:
 *
 *   - User config (server address + device description), stored under the
 *     "clay-settings" key so Clay's generateUrl() pre-fills the form.
 *   - Auth state (a persistent clientId and the token obtained via the SignalK
 *     access-request flow), stored separately under "pebblek-auth" so the token
 *     is never injected into the config web page.
 */

var CONFIG_KEY = "clay-settings";
var AUTH_KEY = "pebblek-auth";
var DEFAULT_DESCRIPTION = "Pebble pebblek";

// Optional build-time overrides (see wscript). The file is generated at build
// time and gitignored, so tolerate its absence.
//   DEV_SERVER (PEBBLEK_DEV_SERVER): wins over the user's Clay config, so the
//     emulator can connect to a hardcoded server without the settings page.
//   DEV_DEVICE_UUID (PEBBLEK_DEVICE_UUID): a static access-request clientId, so
//     the SignalK server recognises the watch across reinstalls (approve once).
//   DEV_TOKEN (PEBBLEK_DEVICE_TOKEN): a pre-minted device token, so the watch
//     skips the access-request/approval flow entirely (no manual approval each
//     emulator launch — its localStorage starts empty). Falls back to the
//     normal flow if the token is rejected (e.g. device not yet approved).
var DEV_SERVER = "";
var DEV_DEVICE_UUID = "";
var DEV_TOKEN = "";
try {
  var buildconfig = require("./buildconfig");
  DEV_SERVER = normalize(buildconfig.devServer);
  DEV_DEVICE_UUID = normalize(buildconfig.deviceUuid);
  DEV_TOKEN = normalize(buildconfig.deviceToken);
} catch (e) {
  DEV_SERVER = "";
  DEV_DEVICE_UUID = "";
  DEV_TOKEN = "";
}

function normalize(field) {
  if (field === null || field === undefined) {
    return "";
  }
  if (typeof field === "object" && "value" in field) {
    field = field.value;
  }
  return ("" + field).trim();
}

function readJson(key) {
  var raw = localStorage.getItem(key);
  if (!raw) {
    return {};
  }
  try {
    return JSON.parse(raw) || {};
  } catch (e) {
    return {};
  }
}

function writeJson(key, obj) {
  localStorage.setItem(key, JSON.stringify(obj));
}

// RFC 4122 v4 UUID, used as the access-request clientId.
function uuid() {
  return "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".replace(/[xy]/g, function (c) {
    var r = (Math.random() * 16) | 0;
    var v = c === "x" ? r : (r & 0x3) | 0x8;
    return v.toString(16);
  });
}

// ---- User config ----------------------------------------------------------

function loadConfig() {
  var p = readJson(CONFIG_KEY);
  return {
    serverAddress: DEV_SERVER || normalize(p.serverAddress),
    description: normalize(p.description) || DEFAULT_DESCRIPTION,
  };
}

function saveConfig(cfg) {
  // Merge to preserve any other fields Clay may persist.
  var existing = readJson(CONFIG_KEY);
  existing.serverAddress = normalize(cfg.serverAddress);
  if (cfg.description !== undefined) {
    existing.description = normalize(cfg.description);
  }
  writeJson(CONFIG_KEY, existing);
}

function hasServer() {
  return loadConfig().serverAddress.length > 0;
}

// ---- Auth state -----------------------------------------------------------

function loadAuth() {
  var p = readJson(AUTH_KEY);
  // A stored token (from a real approval) wins; otherwise fall back to the
  // build-time DEV_TOKEN so a freshly-wiped emulator connects without approval.
  return {
    clientId: normalize(p.clientId) || DEV_DEVICE_UUID,
    token: normalize(p.token) || DEV_TOKEN,
  };
}

// Return the persistent clientId. A build-time DEV_DEVICE_UUID wins (stable
// across reinstalls); otherwise generate and store one on first use.
function ensureClientId() {
  if (DEV_DEVICE_UUID) {
    return DEV_DEVICE_UUID;
  }
  var auth = loadAuth();
  if (!auth.clientId) {
    auth.clientId = uuid();
    writeJson(AUTH_KEY, auth);
  }
  return auth.clientId;
}

function setToken(token) {
  var auth = loadAuth();
  auth.token = normalize(token);
  writeJson(AUTH_KEY, auth);
}

function clearToken() {
  setToken("");
}

function hasToken() {
  return loadAuth().token.length > 0;
}

module.exports = {
  loadConfig: loadConfig,
  saveConfig: saveConfig,
  hasServer: hasServer,
  loadAuth: loadAuth,
  ensureClientId: ensureClientId,
  setToken: setToken,
  clearToken: clearToken,
  hasToken: hasToken,
};
