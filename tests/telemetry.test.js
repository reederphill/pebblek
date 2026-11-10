const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const test = require('node:test');
const protocol = require('../src/pkjs/protocol');

function setup() {
  let now = Date.parse('2026-11-10T12:00:00Z');
  const sent = {};
  class Clock extends Date { static now() { return now; } }
  const context = { module: { exports: {} }, Date: Clock, require(name) {
    return name === './protocol' ? protocol : { set(key, value) { sent[key] = value; } };
  } };
  vm.runInNewContext(fs.readFileSync(require.resolve('../src/pkjs/telemetry'), 'utf8'), context);
  const telemetry = context.module.exports;
  function send(values, age = 0) {
    telemetry.handleMessage({ updates: [{ timestamp: new Date(now - age).toISOString(), values }] });
  }
  return { telemetry, sent, send, advance(ms) { now += ms; }, now: () => now };
}

test('instrument subscription and units include water-referenced true wind', () => {
  const { telemetry, send, sent, now } = setup();
  const paths = telemetry.subscription().subscribe.map(s => s.path);
  assert.ok(paths.includes('navigation.speedThroughWater'));
  assert.ok(paths.includes('environment.wind.angleTrueWater'));
  assert.ok(!paths.includes('environment.wind.angleTrueGround'));
  send([
    { path: 'navigation.speedThroughWater', value: 2.5 },
    { path: 'navigation.speedOverGround', value: 0 },
    { path: 'environment.wind.speedApparent', value: 6 },
    { path: 'environment.wind.angleApparent', value: -Math.PI / 4 },
    { path: 'environment.wind.speedTrue', value: 5 },
    { path: 'environment.wind.angleTrueWater', value: Math.PI / 2 },
  ]);
  assert.equal(sent.STW, 49);
  assert.equal(sent.SOG, 0); // zero is real data, never a missing sentinel
  assert.equal(sent.WIND_SPEED, 117);
  assert.equal(sent.WIND_ANGLE, 315);
  assert.equal(sent.TRUE_WIND_SPEED, 97);
  assert.equal(sent.TRUE_WIND_ANGLE, 90);
  assert.equal(sent.STW_TIME, now() / 1000);
});

test('unchanged measurements retain source time; old updates cannot replace newer samples', () => {
  const { send, sent, advance, now } = setup();
  const path = 'navigation.speedThroughWater';
  send([{ path, value: 2 }]);
  const first = sent.STW_TIME;
  advance(3000);
  send([{ path, value: 2 }], 3000);
  assert.equal(sent.STW_TIME, first);
  send([{ path, value: 3 }]);
  const current = sent.STW;
  send([{ path, value: 9 }], 1000);
  assert.equal(sent.STW, current);
  assert.equal(sent.STW_TIME, now() / 1000);
});

test('null, nonnumeric, negative speeds, unknown pilot and malformed timestamps invalidate independently', () => {
  const { telemetry, send, sent } = setup();
  send([{ path: 'navigation.speedOverGround', value: 3 },
    { path: 'environment.wind.speedApparent', value: 6 },
    { path: 'environment.wind.angleApparent', value: 1 }]);
  send([{ path: 'environment.wind.angleApparent', value: null }]);
  assert.equal(sent.WIND_ANGLE, -1);
  assert.equal(sent.WIND_ANGLE_TIME, 0);
  assert.equal(sent.WIND_SPEED, 117);
  send([{ path: 'navigation.speedThroughWater', value: -2 },
    { path: 'steering.autopilot.state', value: 'bogus' },
    { path: 'environment.wind.speedTrue', value: Infinity },
    { path: 'steering.autopilot.target.headingMagnetic', value: null }]);
  assert.equal(sent.STW_TIME, 0);
  assert.equal(sent.AUTOPILOT_STATE, 255);
  assert.equal(sent.TRUE_WIND_SPEED_TIME, 0);
  assert.equal(sent.TARGET_HEADING, -1);
  telemetry.handleMessage({ updates: [{ timestamp: 'bad', values: [{ path: 'navigation.speedOverGround', value: 4 }] }] });
  assert.equal(sent.SOG_TIME, 0);
  send([{ path: 'navigation.speedOverGround', value: 4 }], -10000);
  assert.equal(sent.SOG_TIME, 0);
});

test('reconnect clears queued values, and replaying expired cache does not revive it', () => {
  const { telemetry, send, sent, advance } = setup();
  send([{ path: 'navigation.speedOverGround', value: 3 }]);
  telemetry.reset();
  assert.equal(sent.SOG, -1);
  assert.equal(sent.SOG_TIME, 0);
  advance(11000);
  send([{ path: 'navigation.speedOverGround', value: 3 }], 11000);
  assert.equal(sent.SOG_TIME, 0);
  send([{ path: 'navigation.speedOverGround', value: 3 }]);
  assert.ok(sent.SOG_TIME > 0);
  assert.equal(sent.SOG, 58);
});

test('malformed deltas and unrelated paths are harmless', () => {
  const { telemetry, sent } = setup();
  [null, {}, { updates: [null, {}, { values: [null, { path: '__proto__' }] }] }].forEach(telemetry.handleMessage);
  assert.deepEqual(sent, {});
});
