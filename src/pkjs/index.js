/*
 * PebbleKit JS — runs on the phone. Responsibilities:
 *   - present the settings page (SignalK server address + access token)
 *   - (task 3+) maintain the SignalK connection and relay data to the watch
 *   - (task 5) translate watch button commands into autopilot REST calls
 */

var Clay = require("@rebble/clay");
var clayConfig = require("./config");
var settings = require("./settings");

// We handle the config events ourselves: the server address and token stay on
// the phone (used to reach SignalK) and are deliberately NOT forwarded to the
// watch, so Clay's automatic AppMessage send to the watch is disabled.
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

Pebble.addEventListener("ready", function () {
  var s = settings.load();
  console.log(
    "pebblek pkjs ready; configured=" + settings.isConfigured(s) +
    (s.serverAddress ? " server=" + s.serverAddress : "")
  );
  // task 3: if configured, open the SignalK connection here.
});

Pebble.addEventListener("showConfiguration", function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener("webviewclosed", function (e) {
  // No response means the user backed out without saving — keep existing settings.
  if (!e || !e.response) {
    return;
  }

  var dict = clay.getSettings(e.response, false);
  settings.save({ serverAddress: dict.serverAddress, token: dict.token });

  var s = settings.load();
  console.log(
    "pebblek settings saved; server=" + s.serverAddress +
    " configured=" + settings.isConfigured(s)
  );
  // task 3: (re)connect to SignalK using the new settings here.
});
