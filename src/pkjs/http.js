/*
 * Tiny XMLHttpRequest JSON helper, shared by auth.js (access-request flow) and
 * autopilot.js (REST PUTs). Kept dependency-free so it works under PebbleKit JS.
 *
 * xhrJson(method, url, headers, body, cb):
 *   - headers: optional object of header name -> value
 *   - body:    optional already-stringified request body
 *   - cb(err, status, data):
 *       err    "open-failed" | "network" | null
 *       status HTTP status code (0 on transport failure)
 *       data   parsed JSON response, or null if absent/unparseable
 */

function xhrJson(method, url, headers, body, cb) {
  var xhr = new XMLHttpRequest();
  try {
    xhr.open(method, url, true);
  } catch (e) {
    cb("open-failed", 0, null);
    return;
  }
  if (headers) {
    for (var h in headers) {
      if (headers.hasOwnProperty(h)) {
        xhr.setRequestHeader(h, headers[h]);
      }
    }
  }
  xhr.onreadystatechange = function () {
    if (xhr.readyState !== 4) {
      return;
    }
    if (xhr.status === 0) {
      cb("network", 0, null);
      return;
    }
    var data = null;
    try {
      data = JSON.parse(xhr.responseText);
    } catch (e) {
      data = null;
    }
    cb(null, xhr.status, data);
  };
  xhr.onerror = function () {
    cb("network", 0, null);
  };
  try {
    xhr.send(body || null);
  } catch (e) {
    cb("network", 0, null);
  }
}

module.exports = {
  xhrJson: xhrJson,
};
