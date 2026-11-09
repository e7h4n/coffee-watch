import test from 'node:test';
import assert from 'node:assert/strict';
import {SERVICE, CONFIG, STATUS, SIGNAL, DROPBOX_CONFIG, DROPBOX_STATUS, dropboxPacket, dropboxAuthorizeUrl, decodeDropboxStatus, joinPacket, forgetPacket, decodeStatus, WatchConnection} from './protocol.mjs';
const dropboxValue = (state = 'unconfigured', request = 0) => new DataView(new TextEncoder().encode(JSON.stringify({v:2,state,request,saved:state==='connected',error:''})).buffer);
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
  const dropboxConfig = {async writeValueWithResponse(p) {this.last = new Uint8Array(p);}};
  const dropboxStatus = new Characteristic();dropboxStatus.value = dropboxValue();
  device.gatt = {connected: false, async connect() {this.connected = true;return this;},
    async getPrimaryService(uuid) {assert.equal(uuid, SERVICE);return {async getCharacteristic(id) {assert.ok([CONFIG, STATUS, SIGNAL, DROPBOX_CONFIG, DROPBOX_STATUS].includes(id));return id === CONFIG ? config : id === STATUS ? characteristic : id === DROPBOX_CONFIG ? dropboxConfig : id === DROPBOX_STATUS ? dropboxStatus : signal;}};},
    disconnect() {this.connected = false;device.dispatchEvent(new Event('gattserverdisconnected'));}};
  return {device, characteristic, signal, config, dropboxConfig, dropboxStatus};
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

test('Dropbox packets carry key, secret and code; status is strictly typed', () => {
  const p = dropboxPacket(1,'abc123def456ghi','0secret0secret0','Xy_9-AbCd',43);
  assert.deepEqual([...p.slice(0,9)],[2,1,15,15,43,0,0,0,9]);
  assert.equal(new TextDecoder().decode(p.slice(9)),'abc123def456ghi0secret0secret0Xy_9-AbCd');
  assert.deepEqual([...dropboxPacket(2,'','','',44)],[2,2,0,0,44,0,0,0,0]);
  assert.throws(() => dropboxPacket(1,'bad-key','secret','code'));
  assert.throws(() => dropboxPacket(1,'key','secret with space','code'));
  assert.throws(() => dropboxPacket(1,'key','secret',''));
  assert.throws(() => dropboxPacket(1,'key','secret','c'.repeat(129)));
  assert.throws(() => dropboxPacket(3,'key','',''));
  assert.equal(dropboxAuthorizeUrl('abc123'),'https://www.dropbox.com/oauth2/authorize?client_id=abc123&response_type=code&token_access_type=offline');
  assert.throws(() => dropboxAuthorizeUrl('abc&redirect_uri=x'));
  assert.equal(decodeDropboxStatus(dropboxValue('connected')).state,'connected');
  const v = JSON.parse(new TextDecoder().decode(dropboxValue()));v.state='awaiting_consent';
  assert.throws(() => decodeDropboxStatus(new TextEncoder().encode(JSON.stringify(v))));
  v.state='connected';v.saved=false;assert.throws(() => decodeDropboxStatus(new TextEncoder().encode(JSON.stringify(v))));
  v.v=1;v.saved=true;assert.throws(() => decodeDropboxStatus(new TextEncoder().encode(JSON.stringify(v))));
});
test('Dropbox writes are erased and only matching device state acknowledges them', async () => {
  const {device,dropboxConfig,dropboxStatus,signal} = fakeDevice();const link = new WatchConnection({requestDevice:async()=>device});
  const received=[];link.addEventListener('dropbox-status',e=>received.push(e.detail));await link.connect();received.length=0;
  const p=dropboxPacket(1,'abc123def456ghi','0secret0secret0','Xy_9-AbCd',43);await link.writeDropbox(p);
  assert.ok(p.every(x=>x===0));assert.equal(dropboxConfig.last[4],43);assert.equal(received.length,0);
  dropboxStatus.value=dropboxValue('authorizing',43);await link.readDropboxStatus();assert.equal(received.at(-1).state,'authorizing');assert.equal(link.pendingDropboxRequest,43);
  dropboxStatus.value=dropboxValue('connected',43);signal.signal();await link.readAll();
  assert.equal(received.at(-1).state,'connected');assert.equal(link.pendingDropboxRequest,0);
  link.disconnect();const p2=dropboxPacket(2,'','','',44);await assert.rejects(link.writeDropbox(p2));assert.ok(p2.every(x=>x===0));
});
