import test from 'node:test';
import assert from 'node:assert/strict';
import {SERVICE, CONFIG, STATUS, joinPacket, forgetPacket, decodeStatus, WatchConnection} from './protocol.mjs';
const status = (state = 'unconfigured', request = 0) => new DataView(new TextEncoder().encode(JSON.stringify({v: 1, state, request, reason: 0, ssid: 'Cafe', ip: state === 'connected' ? '192.168.1.2' : '', saved: state === 'connected'})).buffer);
test('wire bytes preserve UTF-8, spaces, request IDs and password boundaries', () => {
  const p = joinPacket(' Cafe ', 'password', 0x12345678);
  assert.deepEqual([...p.slice(0, 8)], [1, 1, 6, 8, 0x78, 0x56, 0x34, 0x12]);
  assert.equal(new TextDecoder().decode(p.slice(8, 14)), ' Cafe ');
  assert.equal(joinPacket('咖啡', '', 1)[2], 6);
  assert.throws(() => joinPacket('咖'.repeat(11), '', 1));
  assert.throws(() => joinPacket('Cafe', 'short', 1));
  assert.throws(() => joinPacket('Cafe', 'z'.repeat(64), 1));
  assert.throws(() => joinPacket('a\0b', '', 1));
  assert.equal(joinPacket('Cafe', 'a'.repeat(64), 1).length, 76);
  assert.deepEqual([...forgetPacket(1)], [1, 2, 0, 0, 1, 0, 0, 0]);
});
test('only declared network states and actual IP addresses are accepted', () => {
  assert.equal(decodeStatus(status('connected')).state, 'connected');
  assert.throws(() => decodeStatus(status('invented')));
  const missingIP = JSON.parse(new TextDecoder().decode(status('connected')));missingIP.ip = '';
  assert.throws(() => decodeStatus(new TextEncoder().encode(JSON.stringify(missingIP))));
});
class Characteristic extends EventTarget {
  constructor() {super();this.value = status();this.reads = 0;this.notifications = false;}
  async readValue() {this.reads++;this.dispatchEvent(new Event('characteristicvaluechanged'));return this.value;}
  async startNotifications() {this.notifications = true;return this;}
  signal() {this.value = new DataView(Uint8Array.of(1).buffer);this.dispatchEvent(new Event('characteristicvaluechanged'));}
}
function fakeDevice() {
  const characteristic = new Characteristic(), device = new EventTarget();device.name = 'CoffeeWatch-test';
  const config = {async writeValueWithResponse(p) {this.last = new Uint8Array(p);}};
  device.gatt = {connected: false, async connect() {this.connected = true;return this;},
    async getPrimaryService(uuid) {assert.equal(uuid, SERVICE);return {async getCharacteristic(id) {return id === CONFIG ? config : characteristic;}};},
    disconnect() {this.connected = false;device.dispatchEvent(new Event('gattserverdisconnected'));}};
  return {device, characteristic, config};
}
test('reads do not create a notification/read loop; writes need fresh matching status', async () => {
  const {device, characteristic, config} = fakeDevice();
  const link = new WatchConnection({requestDevice: async options => {assert.equal(options.filters[0].services[0], SERVICE);return device;}});
  const received = [];link.addEventListener('status', e => received.push(e.detail));
  await link.connect();assert.equal(characteristic.reads, 2);assert.equal(characteristic.notifications, true);
  received.length = 0;
  const packet = joinPacket('Cafe', 'password', 42);await link.write(packet);
  assert.equal(received.length, 0, 'old pre-request state must not count as the new operation');
  assert.ok(packet.every(x => x === 0));assert.equal(config.last[4], 42);
  characteristic.value = status('connecting', 42);await link.readStatus();assert.equal(received.at(-1).state, 'connecting');
  characteristic.value = status('connected', 42);await link.readStatus();assert.equal(received.at(-1).state, 'connected');
  assert.equal(link.pendingRequest, 0);
  link.disconnect();assert.equal(link.device, null);
});
test('disconnection invalidates pending operations without a retry', async () => {
  const {device} = fakeDevice();const link = new WatchConnection({requestDevice: async () => device});
  await link.connect();link.disconnect();const packet = joinPacket('Cafe', 'password', 8);
  await assert.rejects(link.write(packet));assert.ok(packet.every(x => x === 0));
});
