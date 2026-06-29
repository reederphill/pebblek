# signalk-autopilot-mock

A fake SignalK autopilot for **local pebblek development**. It implements the
[signalk-autopilot](https://github.com/SignalK/signalk-autopilot) REST API in
memory, so the watch → phone → SignalK command loop can be tested without a real
NMEA2000 autopilot on the bus.

It also (by default) simulates the boat — heading, speed over ground and apparent
wind — and steers the simulated heading toward the autopilot target, so engaging
the pilot and nudging the heading from the watch produce **visible** changes on
the dashboard.

## What it serves

Standard `signalk-autopilot` v1 endpoints under
`/signalk/v1/api/vessels/self/steering/autopilot/`:

| Method / path                       | Body                                  | Effect |
| ----------------------------------- | ------------------------------------- | ------ |
| `PUT state`                         | `{"value":"standby\|auto\|wind\|route"}` | set state; on `auto`/`wind` latch the current heading/wind as target |
| `PUT actions/adjustHeading`         | `{"value": <relative degrees>}`       | nudge the target (requires `auto`/`wind`) |
| `PUT target/headingMagnetic`        | `{"value": <radians>}`                | set an absolute heading target |
| `PUT actions/tack`                  | `{"value":"port\|starboard"}`         | flip the apparent-wind target across the bow |
| `PUT actions/advanceWaypoint`       | `{"value":1}`                         | acknowledged (no route model) |

State and target are streamed back on `steering.autopilot.state` /
`steering.autopilot.target.*`, which is what the watch subscribes to.

## Running it

From the repo root it is wired into `compose.yaml` (mounted + `npm link`ed):

```sh
make up        # docker compose up -d  → SignalK server on http://localhost:3000
```

### One-time server setup (security is enabled)

The watch authenticates via the SignalK access-request flow, so the server runs
with `--securityenabled`. On first run, open <http://localhost:3000> and:

1. **Create the admin login** when prompted.
2. **Enable this plugin:** Server → Plugin Config → *Autopilot Mock (pebblek dev)*
   → Enabled → Submit. (Leave *Simulate the boat* on.)
3. **Approve the watch:** after you build/install the app pointing at this server
   (`make dev DEV_SERVER=http://<your-ip>:3000`), the phone sends an access
   request — approve it under Security → Access Requests. The watch then shows
   live telemetry; the UP/DOWN/SELECT buttons drive this mock.

## Config

- `simulateNavigation` (default `true`) — publish simulated nav/wind and steer
  the heading. Turn off to defer telemetry to real data / `--sample-n2k-data`.
- `period` (default `1000` ms) — how often state + telemetry are emitted.
- `turnRateDegPerSec` (default `5`) — how fast the simulated heading swings to target.

> This is a dev-only test double; don't ship it or point a real boat at it.
