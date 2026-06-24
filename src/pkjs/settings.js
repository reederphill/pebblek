/*
 * Persisted phone-side settings: the SignalK server address and access token.
 *
 * Stored under the "clay-settings" localStorage key — the same key Clay's
 * generateUrl() reads to pre-fill the config form — so saved values reappear
 * when the user re-opens settings, giving a single source of truth.
 */

var STORAGE_KEY = "clay-settings";

// Clay returns input values as plain strings, but normalize defensively in case
// a value arrives wrapped as { value: ... }.
function normalize(field) {
  if (field === null || field === undefined) {
    return "";
  }
  if (typeof field === "object" && "value" in field) {
    field = field.value;
  }
  return ("" + field).trim();
}

function load() {
  var parsed = {};
  var raw = localStorage.getItem(STORAGE_KEY);
  if (raw) {
    try {
      parsed = JSON.parse(raw);
    } catch (e) {
      parsed = {};
    }
  }
  return {
    serverAddress: normalize(parsed.serverAddress),
    token: normalize(parsed.token),
  };
}

function save(settings) {
  localStorage.setItem(
    STORAGE_KEY,
    JSON.stringify({
      serverAddress: normalize(settings.serverAddress),
      token: normalize(settings.token),
    })
  );
}

// True once both a server address and a token have been provided.
function isConfigured(settings) {
  settings = settings || load();
  return settings.serverAddress.length > 0 && settings.token.length > 0;
}

module.exports = {
  load: load,
  save: save,
  isConfigured: isConfigured,
};
