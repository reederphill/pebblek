const assert = require('node:assert/strict');
const test = require('node:test');
const Alerts = require('../src/pkjs/alerts');
const http = require('../src/pkjs/http');

const path = 'notifications.navigation.anchor';
const vessel = 'vessels.urn:mrn:signalk:uuid:boat';
function fixture(t) {
  const requests = [], events = [];
  t.mock.method(http, 'xhrJson', (method, url, headers, body, cb) => {
    assert.equal(method, 'GET');
    assert.equal(headers.Authorization, 'Bearer test');
    requests.push({ url, cb });
  });
  const feed = new Alerts(event => events.push(event));
  t.after(() => feed.close());
  feed.setServer('boat:3000');
  function open() {
    feed.open('test');
    feed.handleMessage({ self: vessel });
    return requests.at(-1).cb;
  }
  function delta(value, timestamp, context = vessel, key = path) {
    feed.handleMessage({ context, updates: [{ timestamp, values: [{ path: key, value }] }] });
  }
  function messages() { return events.filter(e => e.ALERT_ID); }
  return { feed, open, delta, events, messages, requests };
}
const alert = (state, message = 'Check anchor') => ({ state, message });

test('subscriptions and alarm activation, repeats, edits, escalation, clear and recurrence', t => {
  assert.deepEqual(Alerts.subscription, { path: 'notifications.*', policy: 'instant', minPeriod: 0 });
  const { open, delta, messages } = fixture(t);
  open()(null, 404, null);
  for (const value of [alert('warn'), alert('warn'), alert('warn', 'Anchor moved'), alert('alert'),
    alert('warn'), alert('alarm'), alert('nominal'), alert('normal'), alert('alarm')]) delta(value);
  const actual = messages();
  assert.deepEqual(actual.map(e => e.ALERT_LEVEL), [2, 2, 1, 2, 3, 0, 3]);
  assert.deepEqual(actual.map(e => e.ALERT_NOTIFY), [1, 0, 0, 0, 2, 0, 3]);
  assert.equal(new Set(actual.map(e => e.ALERT_ID)).size, 1);
  assert.equal(actual[1].ALERT_TEXT, 'Anchor moved');
});

test('initial snapshot and historic replay are quiet; concurrent clear wins a late snapshot', t => {
  const { open, delta, events, messages } = fixture(t);
  const reply = open();
  const earlier = '2020-01-01T01:00:00Z';
  const later = '2020-01-01T01:00:05Z';
  delta(alert('warn'), earlier);
  delta(null, later);
  delta(alert('alarm'), earlier);
  reply(null, 200, { navigation: { anchor: { value: alert('warn'), timestamp: earlier } },
    engine: { temp: { value: alert('alarm', 'Hot'), timestamp: earlier } } });
  assert.deepEqual(messages().map(e => [e.ALERT_LEVEL, e.ALERT_NOTIFY]), [[2, 0], [0, 0], [3, 0]]);
  assert.equal(events.at(-1).ALERT_SYNC, 1);
});

test('new live notification during first snapshot is delivered immediately', t => {
  const { open, delta, messages } = fixture(t);
  const reply = open();
  delta(alert('alarm'), new Date().toISOString());
  reply(null, 200, {});
  assert.deepEqual(messages().map(e => [e.ALERT_LEVEL, e.ALERT_NOTIFY]), [[3, 1]]);
});

test('new alarm first seen by the initial GET announces once when the stream catches up', t => {
  const { open, delta, messages } = fixture(t);
  const reply = open();
  const timestamp = new Date().toISOString();
  const value = alert('alarm');
  reply(null, 200, { navigation: { anchor: { value, timestamp } } });
  delta(value, timestamp);
  assert.deepEqual(messages().map(e => [e.ALERT_LEVEL, e.ALERT_NOTIFY]), [[3, 1]]);
});

test('reconnect corrects missing/escalated/new alarms without reannouncing unchanged ones', t => {
  const { feed, open, delta, messages } = fixture(t);
  open()(null, 404, null);
  for (const key of ['wind', 'depth', 'anchor']) delta(alert('warn'), undefined, vessel, 'notifications.' + key);
  feed.close();
  open()(null, 200, { wind: { value: alert('warn') }, depth: { value: alert('alarm') },
    power: { value: alert('emergency') } });
  assert.deepEqual(messages().slice(3).map(e => [e.ALERT_LEVEL, e.ALERT_NOTIFY]), [[3, 4], [4, 5], [0, 0]]);
});

test('filters other vessels, ignores stale callbacks, bounds UTF-8 payload and replays silently', t => {
  const { feed, open, delta, messages, events } = fixture(t);
  const old = open();
  feed.setServer('other-boat:3000');
  old(null, 200, { stale: { value: alert('emergency') } });
  open()(null, 404, null);
  delta(alert('warn'), undefined, 'vessels.someone-else');
  delta(alert('unknown'));
  delta(alert('warn', '⚓'.repeat(40) + '🚤'));
  delta(alert('warn', 'a'.repeat(108) + '🚤'));
  delta(alert('warn', '🚤'.repeat(40)), undefined, vessel, 'notifications.engine');
  feed.replay();
  assert.deepEqual(messages().map(e => e.ALERT_NOTIFY), [1, 0, 2, 0, 0]);
  for (const message of messages()) {
    assert.ok(Buffer.byteLength(message.ALERT_TEXT) <= 110);
    assert.equal(Buffer.from(message.ALERT_TEXT).toString(), message.ALERT_TEXT);
  }
  assert.equal(events.at(-4).ALERT_SYNC, 2);
  assert.equal(events.at(-1).ALERT_SYNC, 1);
});

test('failure to get the full snapshot never reports an all-clear', t => {
  const { open, feed, events } = fixture(t);
  open()('network', 0, null);
  feed.replay();
  assert.equal(events.at(-1).ALERT_SYNC, 0);
});
