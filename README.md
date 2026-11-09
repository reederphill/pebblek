# PebbleK

![PebbleK — SignalK on your wrist](resources/images/banner.png)

A [SignalK](https://signalk.org/) app for Pebble. Monitor your boat from your wrist and control your autopilot.

Install it from the Pebble App Store: [https://apps.repebble.com/bef8e6181d804c4aaa40c482](https://apps.repebble.com/bef8e6181d804c4aaa40c482)

| Autopilot off | Autopilot engaged |
| :-----------: | :---------------: |
| ![Watch dashboard with the autopilot off](resources/images/screenshot-disengaged.png) | ![Watch dashboard with the autopilot engaged](resources/images/screenshot-engaged.png) |

The watch shows boat **heading**, **autopilot status**, **speed over ground**, and **wind speed & apparent wind angle** (e.g. `S45°` = 45° to starboard, `P120°` = 120° to port). The whole screen turns green while the autopilot is engaged.

While engaged, the autopilot's **target** is shown under the heading — desired vs actual:

- **AUTO:** `SET 145°  +4°` — the target heading, and how far off it you are (`+` = target is to starboard).
- **WIND:** `SET AWA S40°` — the target apparent wind angle.

Course changes are shown immediately and assumed to succeed: the SET readout holds the new target while the autopilot catches up. If the autopilot rejects the change, or doesn't report the new target within 5 s, the watch buzzes long, shows **FAILED** / **REVERTED**, and goes back to the autopilot's actual target.

A ring on the wind wheel's rim also marks the target relative to the bow; in WIND mode the red wind tip sits inside the ring when on target.

The side buttons drive a [signalk-autopilot](https://github.com/SignalK/signalk-autopilot):

| Button | Press  | Action                                                        |
| ------ | ------ | ------------------------------------------------------------- |
| Up     | short  | adjust heading **+1°**                                        |
| Up     | hold   | **+10°**, repeating every 0.4 s while held (running total shown) |
| Down   | short  | adjust heading **−1°**                                        |
| Down   | hold   | **−10°**, repeating every 0.4 s while held (running total shown) |
| Select | short  | engage AUTO / disengage                                       |
| Select | long   | engage WIND / disengage                                       |

Each hold step gives a tiny tick buzz so you can count steps without looking (e.g. hold for 9 ticks ≈ 90°). Commands give a haptic confirmation: a short buzz on success, a long buzz on failure.

## How it works

- The **watch app** is native Pebble C (`src/c/`). It renders the dashboard and sends button commands.
- The **phone app** is PebbleKit JS (`src/pkjs/`). It talks to your SignalK server over a WebSocket, relays telemetry to the watch, and turns button commands into autopilot REST calls. All networking lives here.

The two communicate over Pebble AppMessage.

## Setup

Open the pebblek settings on your phone and enter your **SignalK server address** (`host:port`) and a device name. The app requests **read/write** access via the SignalK [access-request flow](https://signalk.org/specification/1.5.0/doc/access_requests.html); approve it in your server's admin UI (**Security → Access Requests**). No token to copy — once approved the watch connects automatically.

## Building & running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
pebble logs                           # stream watch + phone logs
```

## Target platforms

**emery** (Pebble Time 2) and **gabbro** (Pebble Round 2). Other platforms are not supported.

## Project layout

```
src/c/main.c          Native C watch app: UI, AppMessage, buttons
src/c/protocol.h      Shared message enums/encoding (C side)
src/pkjs/             PebbleKit JS (phone): SignalK client, auth, autopilot, settings
src/pkjs/protocol.js  Shared message enums/encoding (JS side — mirror of protocol.h)
package.json          Project metadata (UUID, platforms, message keys)
wscript               Build rules — usually no need to edit
```
