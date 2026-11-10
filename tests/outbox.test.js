const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const test = require('node:test');

function transport() {
  const timers = [];
  const packets = [];
  const context = {
    module: { exports: {} },
    setTimeout(fn) { timers.push(fn); return timers.length; },
    Pebble: { sendAppMessage(data, ack, nack) { packets.push({ data, ack, nack }); } }
  };
  vm.runInNewContext(fs.readFileSync(require.resolve('../src/pkjs/outbox'), 'utf8'), context);
  return {
    box: context.module.exports,
    send() {
      assert.equal(timers.length, 1);
      timers.shift()();
      return packets.shift();
    },
  };
}

test('simultaneous alerts stay ordered through a nack while the newest telemetry wins', () => {
  const { box, send } = transport();
  box.set('HEADING', 10);
  box.event({ ALERT_ID: 1, ALERT_NOTIFY: 5 });
  box.event({ ALERT_ID: 2, ALERT_NOTIFY: 6 });
  const first = send();
  box.set('HEADING', 11);
  first.nack();
  const retry = send();
  assert.deepEqual({ ...retry.data }, { HEADING: 11, ALERT_ID: 1, ALERT_NOTIFY: 5 });
  retry.ack();
  assert.deepEqual({ ...send().data }, { ALERT_ID: 2, ALERT_NOTIFY: 6 });
});

test('a reset replaces queued alerts and prevents retrying a superseded incident', () => {
  const { box, send } = transport();
  box.event({ ALERT_ID: 1 });
  const flight = send();
  box.event({ ALERT_ID: 2 });
  box.event({ ALERT_SYNC: 2 });
  box.event({ ALERT_SYNC: 1 });
  flight.nack();
  const reset = send();
  assert.deepEqual({ ...reset.data }, { ALERT_SYNC: 2 });
  reset.ack();
  assert.deepEqual({ ...send().data }, { ALERT_SYNC: 1 });
});
