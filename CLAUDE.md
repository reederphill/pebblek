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

## Local development & testing

The `Makefile` wraps the dev loop; `make help` lists targets. The core workflow:

```sh
make dev                  # the one command: (re)launch the emulator end-to-end
make dev PLATFORM=gabbro  # same, on the round emulator (default is emery)
make up / make down       # start/stop just the SignalK server
make logs                 # stream watch APP_LOG + pkjs console.log
```

**Always relaunch with `make dev`** (not a bare `pebble install`). It bundles everything the dev loop needs and any of these done piecemeal leaves the emulator wedged or unauthenticated:

1. `docker compose up -d` — a local SignalK server (`compose.yaml`) with the `dev/signalk-autopilot-mock` plugin simulating the boat (telemetry + autopilot). Config (approved devices, `secretKey`) persists in the `signalk` Docker volume.
2. `pebble kill` — resets the emulator.
3. Bakes three build-time values into the gitignored `src/pkjs/buildconfig.js` (via `wscript`, read by `settings.js`). These must be set at **build** time and are kept on the install step too, so its "build if needed" pass doesn't regenerate `buildconfig.js` without them:
   - `PEBBLEK_DEV_SERVER` (`DEV_SERVER`) — hardcodes the server, skipping the Clay settings page.
   - `PEBBLEK_DEVICE_UUID` (`SIGNALK_DEVICE_UUID`) — a **static** access-request clientId instead of a random per-device one.
   - `PEBBLEK_DEVICE_TOKEN` (`SIGNALK_DEVICE_TOKEN`) — a pre-minted device token so the watch **skips the access-request/approval flow** on every launch (the emulator's pkjs localStorage starts empty, so it would otherwise re-request and need manual approval each time).

**Why the baked token:** signalk-server has no config to auto-approve device requests — `requestAccess` always creates a PENDING request. But it mints device tokens as `jwt.sign({ device: clientId }, secretKey)` (no expiry), so `dev/signalk-token.sh` reads the running server's `secretKey` from its `security.json` and re-mints that token for the static clientId. `settings.loadAuth()` prefers a stored token, else falls back to this `DEV_TOKEN`; if it's rejected (e.g. device not yet approved) the watch falls back to the normal access-request flow. The static clientId still needs a **one-time** manual approval per Docker volume (it persists in the volume; a `docker compose down -v` wipes it and requires re-approving once). Production builds (plain `pebble build`) leave all three empty → normal user-configured behaviour.

**Screenshots:** `pebble screenshot --emulator emery --no-open <path>` (always pass `--no-open`). The qemu/pypkjs screenshot transport is flaky and times out intermittently — if it doesn't print "Saved", just run the same command again (as a separate call, not a shell `for` loop — see below).

**Running the dev tools without permission prompts:** `.claude/settings.json` allowlists `make *`, `pebble *`, `docker compose *`, and the read-only helpers `tail *` / `head *` / `ls *`. These prefix rules only match when the tool is the **start of a plain command** — so keep commands simple and one tool per call:

- Run `make dev`, `pebble screenshot …`, etc. as standalone commands. Piping `make dev` into anything other than the allowlisted `tail`/`head` (e.g. `| grep`, `| sed`), chaining unrelated tools with `&&`, or wrapping calls in a `for`/`while` loop all fall outside the rules and trigger a prompt.
- To wait for the emulator to connect or to retry a flaky screenshot, issue repeated **separate** `pebble screenshot` calls instead of a shell retry loop.
- New tools you find yourself running often for the dev loop belong in `.claude/settings.json` (committed, team-wide), not in a one-off compound command.

**Displays:** emery (Pebble Time 2) is 200×228 rectangular; gabbro (Pebble Round 2) is 260×260 round (rounded corners clip edges, so `ui.c` centers content as a group on round).

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

`src/pkjs/protocol.js` (phone) and `src/c/protocol.h` (watch) are **mirrors** — keep their enum values (`AP_STATE`/`ApState`, `CONN`/`ConnStatus`, `CMD_RESULT`/`CmdResult`) and encoding identical. Encoding (all values are ints): heading & wind-angle = degrees 0..359; SOG & wind-speed = tenths of a knot (`SPEED_SCALE` = 10); autopilot state & connection status = enums. Autopilot targets (phone→watch): `TARGET_HEADING` / `TARGET_WIND_ANGLE` in degrees 0..359, or `TARGET_NONE` (-1) when SignalK reports null. Commands (watch→phone): `CMD_ADJUST_HEADING` carries relative degrees (±1, or a multiple of 10 — `comm.c` queues commands behind the one-in-flight AppMessage and sums deltas from a held button); `CMD_TOGGLE_ENGAGE` = 1. Ack (phone→watch): `CMD_RESULT`.

## Gotchas

- **Optimistic targets (`src/c/target.c`).** Heading nudges update the displayed target immediately; incoming `TARGET_*` telemetry is ignored until it matches (±1°), or reverted with an alert on `CMD_RESULT` FAILED / 5 s timeout. The phone keeps a coalesced FAILED sticky (`autopilot.js` `sendResult`) so a later OK can't hide it. `WIND_ADJUST_SIGN` (which way a nudge moves the target wind angle) matches the mock and is unverified on real hardware.

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
