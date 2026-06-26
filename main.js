/*
 * pebblek watch app — SignalK boat monitor + autopilot remote (watch side).
 *
 * Wiring:
 *   messaging.js  — receives telemetry AppMessages from the phone and sends
 *                   autopilot command AppMessages back.
 *   state.js      — central app state; telemetry mutates it and a single
 *                   onChange callback requests a redraw (NO per-second timer).
 *   render.js     — draws the dashboard / status screens with Poco.
 *   buttons.js    — physical buttons -> autopilot commands with single/long
 *                   press distinction and optimistic on-screen feedback.
 *
 * Globals provided by the Pebble host (see SDK host/main.js): `screen` (the
 * commodetto pixel destination) and `watch` (the Pebble event object). We do
 * not use the time events here — redraws are driven purely by state changes.
 */

import Timer from "timer";
import State from "state";
import Renderer from "render";
import Messaging from "messaging";
import Buttons from "buttons";

const renderer = new Renderer();

// Coalesce multiple state changes within a tick into a single redraw.
let redrawScheduled = false;
function requestRedraw() {
	console.log("requestRedraw");
	if (redrawScheduled) return;
	redrawScheduled = true;
	Timer.set(() => {
		redrawScheduled = false;
		renderer.render(state);
	});
}

const state = new State(requestRedraw);

const messaging = new Messaging((dict) => state.apply(dict));

// Send a command and show optimistic feedback immediately; cleared/confirmed
// when CMD_RESULT arrives (handled in state.apply) or after a timeout.
function adjustHeading(delta) {
	state.setPending((delta > 0 ? "+" : "") + delta + "°");
	messaging.send({ CMD_ADJUST_HEADING: delta });
}

function toggleEngage() {
	state.setPending("ENGAGE");
	messaging.send({ CMD_TOGGLE_ENGAGE: 1 });
}

// Button -> command mapping (task 9):
//   UP    press        -> +1     UP    long -> +10
//   DOWN  press        -> -1     DOWN  long -> -10
//   SELECT press       -> toggle engage
new Buttons({
	up: {
		press: () => adjustHeading(1),
		long: () => adjustHeading(10),
	},
	down: {
		press: () => adjustHeading(-1),
		long: () => adjustHeading(-10),
	},
	select: {
		press: () => toggleEngage(),
	},
});

console.log("Hello, Watchface.");

// Initial paint (shows the disconnected/initial status until telemetry arrives).
requestRedraw();
