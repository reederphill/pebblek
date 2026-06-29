/*
 * signalk-autopilot-mock — a fake autopilot for local pebblek development.
 *
 * The real @signalk/signalk-autopilot plugin only answers commands when there is
 * an actual NMEA2000/SeaTalk autopilot on the bus, so it can't be exercised on a
 * laptop. This mock implements the same REST contract (see CLAUDE.md "signalk-
 * autopilot API") entirely in memory so the watch -> phone -> SignalK command
 * loop can be tested locally:
 *
 *   PUT /signalk/v1/api/vessels/self/steering/autopilot/state
 *       { "value": "standby" | "auto" | "wind" | "route" }
 *   PUT /signalk/v1/api/vessels/self/steering/autopilot/actions/adjustHeading
 *       { "value": <relative degrees, e.g. 1 / 10 / -1 / -10> }
 *   PUT /signalk/v1/api/vessels/self/steering/autopilot/target/headingMagnetic
 *       { "value": <radians> }                                 (absolute target)
 *   PUT /signalk/v1/api/vessels/self/steering/autopilot/actions/tack
 *       { "value": "port" | "starboard" }
 *   PUT /signalk/v1/api/vessels/self/steering/autopilot/actions/advanceWaypoint
 *       { "value": 1 }
 *
 * SignalK routes a PUT on .../steering/autopilot/<x>/<y> to the handler we
 * register for the path steering.autopilot.<x>.<y>, so registering the handlers
 * below is all that's needed to serve those URLs.
 *
 * It also (optionally) simulates the boat — heading, speed over ground and
 * apparent wind — and steers the simulated heading toward the autopilot target,
 * so engaging and nudging the heading from the watch produces VISIBLE changes in
 * the dashboard instead of replaying a fixed recording. Turn this off
 * (simulateNavigation: false) to defer telemetry to real data / --sample-n2k-data.
 *
 * Modelled on the emulator in @signalk/signalk-autopilot but standalone and
 * dependency-free so it can be npm-link'd into the container with no install.
 */

'use strict'

const STATE_PATH = 'steering.autopilot.state'
const TARGET_HEADING_PATH = 'steering.autopilot.target.headingMagnetic'
const TARGET_WIND_PATH = 'steering.autopilot.target.windAngleApparent'
const ADJUST_HEADING_PATH = 'steering.autopilot.actions.adjustHeading'
const TACK_PATH = 'steering.autopilot.actions.tack'
const ADVANCE_PATH = 'steering.autopilot.actions.advanceWaypoint'

const VALID_STATES = ['standby', 'auto', 'wind', 'route']

const DEG = Math.PI / 180
const KNOT = 0.514444 // m/s per knot

function ok() {
  return { state: 'COMPLETED', statusCode: 200 }
}

function fail(message) {
  return { state: 'COMPLETED', statusCode: 400, message }
}

// Wrap an angle (radians) into 0..2π — keeps headingMagnetic in SignalK's range.
function norm2pi(rad) {
  const twoPi = 2 * Math.PI
  return ((rad % twoPi) + twoPi) % twoPi
}

// Wrap an angle (radians) into -π..π — used for apparent wind, relative to the bow.
function normPi(rad) {
  return Math.atan2(Math.sin(rad), Math.cos(rad))
}

module.exports = function (app) {
  // Autopilot control state.
  let currentState = 'standby'
  let targetHeading = 0 // radians, magnetic
  let targetWind = 0 // radians, apparent, relative to the bow

  // Simulated boat state (only published when simulateNavigation is on).
  let heading = 0 // radians, magnetic
  let sog = 5 * KNOT // m/s
  let windAngle = 45 * DEG // radians, apparent
  let windSpeed = 12 * KNOT // m/s

  let timer = null
  let options = {}

  // Emit a SignalK delta from this plugin as the source.
  function emit(values) {
    app.handleMessage(plugin.id, { updates: [{ values: values }] })
  }

  // Publish the current autopilot state (and active target). Sent immediately on
  // every change and once per heartbeat so a freshly (re)subscribed client — like
  // the watch after a reconnect — always gets the latest state promptly.
  function publishAutopilot() {
    const values = [{ path: STATE_PATH, value: currentState }]
    if (currentState === 'auto') {
      values.push({ path: TARGET_HEADING_PATH, value: targetHeading })
    } else if (currentState === 'wind') {
      values.push({ path: TARGET_WIND_PATH, value: targetWind })
    }
    emit(values)
  }

  // --- REST PUT handlers ----------------------------------------------------
  // Each returns synchronously; SignalK turns { statusCode } into the HTTP reply.

  function putState(context, path, value) {
    if (VALID_STATES.indexOf(value) === -1) {
      return fail('invalid state "' + value + '"')
    }
    if (value === 'auto') {
      // Latch onto the current heading, like a real autopilot engaging.
      targetHeading = simEnabled()
        ? heading
        : app.getSelfPath('navigation.headingMagnetic.value') || 0
    } else if (value === 'wind') {
      targetWind = simEnabled()
        ? windAngle
        : app.getSelfPath('environment.wind.angleApparent.value') || 0
    }
    currentState = value
    app.debug('state -> %s', currentState)
    publishAutopilot()
    return ok()
  }

  function putAdjustHeading(context, path, value) {
    const delta = Number(value)
    if (!isFinite(delta)) {
      return fail('adjustHeading value must be a number (relative degrees)')
    }
    if (currentState === 'auto') {
      targetHeading = norm2pi(targetHeading + delta * DEG)
      app.debug('adjustHeading %d° -> target %d°', delta, Math.round(targetHeading / DEG))
      emit([{ path: TARGET_HEADING_PATH, value: targetHeading }])
      return ok()
    }
    if (currentState === 'wind') {
      targetWind = normPi(targetWind + delta * DEG)
      app.debug('adjustHeading %d° -> wind target %d°', delta, Math.round(targetWind / DEG))
      emit([{ path: TARGET_WIND_PATH, value: targetWind }])
      return ok()
    }
    return fail('autopilot not engaged (state is "' + currentState + '")')
  }

  function putTargetHeading(context, path, value) {
    const v = Number(value)
    if (!isFinite(v)) {
      return fail('target headingMagnetic must be a number (radians)')
    }
    targetHeading = norm2pi(v)
    if (currentState === 'auto') {
      emit([{ path: TARGET_HEADING_PATH, value: targetHeading }])
    }
    return ok()
  }

  function putTack(context, path, value) {
    if (currentState !== 'wind' && currentState !== 'auto') {
      return fail('autopilot not in wind or auto mode')
    }
    // Reflect the apparent-wind target across the bow — the boat coming about.
    targetWind = normPi(-targetWind)
    if (simEnabled()) {
      windAngle = normPi(-windAngle)
    }
    app.debug('tack %s', value)
    emit([{ path: TARGET_WIND_PATH, value: targetWind }])
    return ok()
  }

  function putAdvanceWaypoint(context, path, value) {
    if (currentState !== 'route') {
      return fail('autopilot not in route mode')
    }
    // No route model in the mock — just acknowledge so round-trip flows pass.
    app.debug('advanceWaypoint')
    return ok()
  }

  // --- Boat simulation ------------------------------------------------------

  function simEnabled() {
    return options.simulateNavigation !== false
  }

  // Move `from` toward `to` by at most `maxStep` (all radians); shortest way round.
  function steer(from, to, maxStep) {
    const diff = normPi(to - from)
    if (Math.abs(diff) <= maxStep) {
      return to
    }
    return from + Math.sign(diff) * maxStep
  }

  // One simulation tick: advance the boat and publish telemetry.
  function tick() {
    if (simEnabled()) {
      const rate = (options.turnRateDegPerSec || 5) * DEG // how fast we swing to target

      if (currentState === 'auto') {
        heading = norm2pi(steer(heading, targetHeading, rate))
      } else if (currentState === 'wind') {
        // Hold the apparent wind angle at target by yawing the boat.
        const corr = steer(windAngle, targetWind, rate)
        heading = norm2pi(heading + (corr - windAngle))
        windAngle = corr
      } else {
        // Standby: let the boat wander a little so the display feels alive.
        heading = norm2pi(heading + (Math.random() - 0.5) * 2 * DEG)
      }

      // A touch of noise on speed/wind so the dashboard isn't dead still.
      const jitter = (base, amp) => base + (Math.random() - 0.5) * amp
      emit([
        { path: 'navigation.headingMagnetic', value: heading },
        { path: 'navigation.speedOverGround', value: jitter(sog, 0.3 * KNOT) },
        { path: 'environment.wind.angleApparent', value: normPi(jitter(windAngle, 1 * DEG)) },
        { path: 'environment.wind.speedApparent', value: jitter(windSpeed, 0.5 * KNOT) }
      ])
    }

    publishAutopilot()
  }

  const plugin = {
    id: 'signalk-autopilot-mock',
    name: 'Autopilot Mock (pebblek dev)',
    description:
      'Fake autopilot that answers the signalk-autopilot REST API in memory and ' +
      'optionally simulates the boat, for local pebblek testing.',

    schema: {
      type: 'object',
      properties: {
        simulateNavigation: {
          type: 'boolean',
          title: 'Simulate the boat (heading, speed, wind)',
          description:
            'Publish simulated navigation/wind data and steer the heading toward ' +
            'the autopilot target. Turn off to use real data / --sample-n2k-data.',
          default: true
        },
        period: {
          type: 'number',
          title: 'Update period (ms)',
          description: 'How often autopilot state and simulated telemetry are emitted.',
          default: 1000
        },
        turnRateDegPerSec: {
          type: 'number',
          title: 'Simulated turn rate (deg/s)',
          description: 'How quickly the simulated heading swings toward the target.',
          default: 5
        }
      }
    },

    start: function (opts) {
      options = opts || {}
      const period = options.period || 1000

      app.registerPutHandler('vessels.self', STATE_PATH, putState)
      app.registerPutHandler('vessels.self', ADJUST_HEADING_PATH, putAdjustHeading)
      app.registerPutHandler('vessels.self', TARGET_HEADING_PATH, putTargetHeading)
      app.registerPutHandler('vessels.self', TACK_PATH, putTack)
      app.registerPutHandler('vessels.self', ADVANCE_PATH, putAdvanceWaypoint)

      // Advertise the selectable states (used by SignalK clients / the V2 API).
      app.handleMessage(plugin.id, {
        updates: [
          {
            meta: [
              {
                path: STATE_PATH,
                value: {
                  displayName: 'Autopilot State',
                  type: 'multiple',
                  possibleValues: VALID_STATES.map(function (s) {
                    return { title: s, value: s }
                  })
                }
              }
            ]
          }
        ]
      })

      publishAutopilot()
      timer = setInterval(tick, period)
      app.setPluginStatus(
        'Mock autopilot running' + (simEnabled() ? ' (simulating boat)' : '')
      )
    },

    stop: function () {
      if (timer) {
        clearInterval(timer)
        timer = null
      }
    }
  }

  return plugin
}
