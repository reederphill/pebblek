# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

pebblek is a SignalK app for Pebble — monitor a boat (heading, autopilot status, speed over ground, wind speed/direction) and control the autopilot from the wrist. It is a **native C Pebble app** (`projectType: native`) on the watch, paired with **PebbleKit JS** on the phone for SignalK connectivity.

> History: the watch side was originally Moddable XS (embedded JavaScript). It was rewritten in native C after sustained Moddable memory/tooling problems (XS machine heap limits, debug-build OOM). Don't reintroduce Moddable. The phone side (`src/pkjs`) is unchanged from that era and was fully reused.

## Commands

```sh
pebble build                          # build for all targetPlatforms (emery, gabbro)
pebble install --emulator emery       # build (if needed) + run on the emery emulator
pebble install --phone <ip>           # install to a paired phone over the network
pebble logs                           # stream APP_LOG (watch) + console.log (pkjs)
```

There is no separate lint/test tooling configured. The harmless `LOAD segment with RWX permissions` linker warning appears on every build.

## Architecture: two runtimes

The watch and phone run different code; understanding the split is essential.

1. **Watch — native C (`src/c/`).** `main.c` is the whole watch app: a `Window` with one rendering `Layer`, AppMessage in/out, native button handling, and an `AppTimer` for transient feedback. `protocol.h` holds the enums (`ApState`, `ConnStatus`, `CmdResult`) and `SPEED_SCALE`. No watch-side network access — all SignalK traffic is on the phone.

2. **Phone — PebbleKit JS (`src/pkjs/`).** Classic `Pebble.addEventListener(...)` API. This is the only side with network access. It connects to the SignalK server, relays telemetry to the watch, and turns the watch's button commands into autopilot REST calls.

Watch ↔ phone messaging is **AppMessage**, keyed by the names in `package.json` → `pebble.messageKeys`. In C those surface as `MESSAGE_KEY_<NAME>`; in pkjs they're used by name.

## Phone module map (`src/pkjs/`, CommonJS)

- `index.js` — orchestrator: Clay settings events and the connect lifecycle (`start` → access request → token validation → stream), wiring the modules below.
- `auth.js` — SignalK **access-request** auth: POST `{ clientId, description, permissions: "readwrite" }`, poll until the admin approves, store the token; `validateToken` detects 401/403 (revoked).
- `signalk.js` — `SignalKClient`: WebSocket to the SignalK stream with the token in `?token=` (browser WS can't set an Authorization header), reconnect with backoff, `onOpen`/`onMessage` hooks.
- `telemetry.js` — builds the SignalK subscription and parses deltas (rad→deg, m/s→tenths-of-knot, autopilot state string→enum).
- `autopilot.js` — translates watch button commands into `signalk-autopilot` REST PUTs (`Authorization: Bearer <token>`).
- `outbox.js` — one-AppMessage-in-flight queue (coalesces telemetry, waits for ack/nack).
- `settings.js` / `config.js` — persisted config (server address + device name) and the Clay form; `http.js` — shared `xhrJson`; `state.js` — last-known autopilot state for the toggle.
- `protocol.js` — the AppMessage value contract (enums + encoding), required as `./protocol`.

## The AppMessage contract

`src/pkjs/protocol.js` (phone) and `src/c/protocol.h` (watch) are **mirrors** — keep their enum values (`AP_STATE`/`ApState`, `CONN`/`ConnStatus`, `CMD_RESULT`/`CmdResult`) and encoding identical. Encoding (all values are ints): heading & wind-angle = degrees 0..359; SOG & wind-speed = tenths of a knot (`SPEED_SCALE` = 10); autopilot state & connection status = enums. Commands (watch→phone): `CMD_ADJUST_HEADING` carries relative degrees (±1/±10); `CMD_TOGGLE_ENGAGE` = 1. Ack (phone→watch): `CMD_RESULT`.

## Gotchas

- **`MESSAGE_KEY_*` are runtime `extern uint32_t` variables, not `#define` constants** (see `build/include/message_keys.auto.h`). Use them as `dict_find`/`dict_write_int` arguments only — they can't appear in a C `switch`/`case`.
- **`@rebble/clay`** (installed via `pebble package install`, not npm) provides the phone settings page. The legacy npm `pebble-clay` doesn't support gabbro and breaks the build.
- PebbleKit JS `sendAppMessage` packs integers into the smallest int width that fits; the C `read_int()` helper in `main.c` reads any width. Don't assume int32.
- Still unverified against real hardware/server: the `?token=` WS auth, whether `signalk-autopilot`'s `adjustHeading` wants degrees vs radians, and the apparent-wind-angle sign/reference.

## Build wiring (`wscript`)

`wscript` (waf) rarely needs editing. Source routing — changing directory layout means changing these globs:
- C app sources: `src/c/**/*.c`
- pkjs bundle: `src/pkjs/**/*.js`, `src/pkjs/**/*.json`, entry `src/pkjs/index.js`
- A `worker_src/` directory, if present, triggers a background worker build.

## Target platforms

Only **emery** (Pebble Time 2) and **gabbro** (Pebble Round 2) are in `targetPlatforms`. `main.c` adapts its layout for round (gabbro) vs rectangular (emery) via `PBL_IF_ROUND_ELSE`.

## Reference

Full SDK docs: <https://developer.repebble.com> · signalk-autopilot API: <https://github.com/SignalK/signalk-autopilot> · SignalK access requests: <https://signalk.org/specification/1.5.0/doc/access_requests.html>
