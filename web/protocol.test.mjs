import test from 'node:test';
import assert from 'node:assert/strict';
import {SERVICE, CONFIG, STATUS, SIGNAL, DRIVE_CONFIG, DRIVE_STATUS, drivePacket, decodeDriveStatus, joinPacket, forgetPacket, decodeStatus, WatchConnection} from './protocol.mjs';
const driveValue = (state = 'unconfigured', request = 0) => new DataView(new TextEncoder().encode(JSON.stringify({v:1,state,request,saved:state==='connected',error:'',user_code:'',verification_url:'',folder:state==='connected'?'folder-id':''})).buffer);
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
  const characteristic = new Characteristic(), signal = new Characteristic(), device = new EventTarget();device.name = 'CoffeeWatch-test';
  const config = {async writeValueWithResponse(p) {this.last = new Uint8Array(p);}};
  const driveConfig = {async writeValueWithResponse(p) {this.last = new Uint8Array(p);}};
  const driveStatus = new Characteristic();driveStatus.value = driveValue();
  device.gatt = {connected: false, async connect() {this.connected = true;return this;},
    async getPrimaryService(uuid) {assert.equal(uuid, SERVICE);return {async getCharacteristic(id) {assert.ok([CONFIG, STATUS, SIGNAL, DRIVE_CONFIG, DRIVE_STATUS].includes(id));return id === CONFIG ? config : id === STATUS ? characteristic : id === DRIVE_CONFIG ? driveConfig : id === DRIVE_STATUS ? driveStatus : signal;}};},
    disconnect() {this.connected = false;device.dispatchEvent(new Event('gattserverdisconnected'));}};
  return {device, characteristic, signal, config, driveConfig, driveStatus};
}
test('reads do not create a notification/read loop; writes need fresh matching status', async () => {
  const {device, characteristic, signal, config} = fakeDevice();
  const link = new WatchConnection({requestDevice: async options => {assert.equal(options.filters[0].services[0], SERVICE);return device;}});
  const received = [];link.addEventListener('status', e => received.push(e.detail));
  await link.connect();assert.equal(characteristic.reads, 2);assert.equal(characteristic.notifications, false);assert.equal(signal.notifications, true);
  received.length = 0;
  const packet = joinPacket('Cafe', 'password', 42);await link.write(packet);
  assert.equal(received.length, 0, 'old pre-request state must not count as the new operation');
  assert.ok(packet.every(x => x === 0));assert.equal(config.last[4], 42);
  characteristic.value = status('connecting', 42);await link.readStatus();assert.equal(received.at(-1).state, 'connecting');
  characteristic.value = status('connected', 42);await link.readStatus();assert.equal(received.at(-1).state, 'connected');
  characteristic.value = status('connected', 42);signal.signal();await link.queue;
  assert.equal(received.at(-1).state, 'connected');assert.equal(characteristic.reads, 6);
  assert.equal(link.pendingRequest, 0);
  link.disconnect();assert.equal(link.device, null);
});
test('disconnection invalidates pending operations without a retry', async () => {
  const {device} = fakeDevice();const link = new WatchConnection({requestDevice: async () => device});
  await link.connect();link.disconnect();const packet = joinPacket('Cafe', 'password', 8);
  await assert.rejects(link.write(packet));assert.ok(packet.every(x => x === 0));
});

test('a state notification arriving during a JSON read cannot replace its response', async () => {
  const {device, characteristic, signal} = fakeDevice();
  const link = new WatchConnection({requestDevice: async () => device});
  const received = [], errors = [];
  link.addEventListener('status', e => received.push(e.detail));
  link.addEventListener('error', e => errors.push(e.detail));
  await link.connect();
  let notifyDuringRead = true;
  characteristic.value = status('connected');
  characteristic.readValue = async () => {
    characteristic.reads++;
    if (notifyDuringRead) {notifyDuringRead = false;signal.signal();await Promise.resolve();}
    return characteristic.value;
  };
  await link.readStatus();await link.queue;
  assert.equal(received.at(-1).state, 'connected');
  assert.equal(signal.value.byteLength, 1);
  assert.deepEqual(errors, []);
  link.disconnect();
});

test('Drive credential packets reject malformed values and status never trusts arbitrary links', () => {
  const p = drivePacket(1,'123-test.apps.googleusercontent.com','test-secret',43);
  assert.deepEqual([...p.slice(0,8)],[1,1,35,11,43,0,0,0]);
  assert.equal(drivePacket(2,'','',44).length,8);
  assert.throws(() => drivePacket(1,'not-a-client','test'));
  assert.throws(() => drivePacket(1,'a.apps.googleusercontent.com','secret with spaces'));
  assert.throws(() => drivePacket(1,'x'.repeat(129)+'.apps.googleusercontent.com','secret'));
  assert.throws(() => drivePacket(2,'a','b'));
  assert.equal(decodeDriveStatus(driveValue('connected')).folder,'folder-id');
  const v = JSON.parse(new TextDecoder().decode(driveValue()));v.state='awaiting_consent';v.user_code='ABCD-EFGH';v.verification_url='https://example.com/device';
  assert.throws(() => decodeDriveStatus(new TextEncoder().encode(JSON.stringify(v))));
  v.verification_url='https://www.google.com/device';assert.equal(decodeDriveStatus(new TextEncoder().encode(JSON.stringify(v))).user_code,'ABCD-EFGH');
});
test('Drive writes are erased and only matching device state acknowledges them', async () => {
  const {device,driveConfig,driveStatus,signal} = fakeDevice();const link = new WatchConnection({requestDevice:async()=>device});
  const received=[];link.addEventListener('drive-status',e=>received.push(e.detail));await link.connect();received.length=0;
  const p=drivePacket(1,'123-test.apps.googleusercontent.com','test-secret',43);await link.writeDrive(p);
  assert.ok(p.every(x=>x===0));assert.equal(driveConfig.last[4],43);assert.equal(received.length,0);
  driveStatus.value=driveValue('connected',43);signal.signal();await link.readAll();
  assert.equal(received.at(-1).state,'connected');assert.equal(link.pendingDriveRequest,0);
  link.disconnect();const p2=drivePacket(2,'','',44);await assert.rejects(link.writeDrive(p2));assert.ok(p2.every(x=>x===0));
});
