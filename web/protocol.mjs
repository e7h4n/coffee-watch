// SPDX-License-Identifier: MIT
export const SERVICE = 'c9a81001-9a9d-4f55-a621-5b4e7699b647';
export const CONFIG = 'c9a81002-9a9d-4f55-a621-5b4e7699b647';
export const STATUS = 'c9a81003-9a9d-4f55-a621-5b4e7699b647';
export const SIGNAL = 'c9a81004-9a9d-4f55-a621-5b4e7699b647';
export const DROPBOX_CONFIG = 'c9a81005-9a9d-4f55-a621-5b4e7699b647';
export const DROPBOX_STATUS = 'c9a81006-9a9d-4f55-a621-5b4e7699b647';
const encoder = new TextEncoder();
const requestId = () => crypto.getRandomValues(new Uint32Array(1))[0] || 1;
export function joinPacket(ssid, password, id = requestId()) {
  if (!Number.isInteger(id) || id <= 0 || id > 0xffffffff) throw new Error('Invalid request ID.');
  if (ssid.includes('\0') || password.includes('\0')) throw new Error('Network details cannot contain a null character.');
  const name = encoder.encode(ssid), secret = encoder.encode(password);
  if (!name.length || name.length > 32) throw new Error('Network name must be between 1 and 32 UTF-8 bytes.');
  if (secret.length && (secret.length < 8 || secret.length > 64)) throw new Error('Use an empty password for an open network, or an 8–63 byte password.');
  if (secret.length === 64 && !/^[a-fA-F0-9]{64}$/.test(password)) throw new Error('A 64-character key must contain hexadecimal digits only.');
  const packet = new Uint8Array(8 + name.length + secret.length);
  packet.set([1, 1, name.length, secret.length]);new DataView(packet.buffer).setUint32(4, id, true);packet.set(name, 8);packet.set(secret, 8 + name.length);secret.fill(0);
  return packet;
}
export function forgetPacket(id = requestId()) {if (!Number.isInteger(id) || id <= 0 || id > 0xffffffff) throw new Error('Invalid request ID.');const p = new Uint8Array([1, 2, 0, 0, 0, 0, 0, 0]);new DataView(p.buffer).setUint32(4, id, true);return p;}
const APP_CREDENTIAL = /^[A-Za-z0-9]{1,32}$/;
// Dropbox shows the authorization code on its own page when no redirect URI is
// given; token_access_type=offline makes the code exchange return a refresh token.
// Requesting only these file scopes keeps any team scope enabled on the app out
// of the request, which would otherwise demand a work (Business) account.
// files.metadata.read lets a device list folders in its app folder (Cardputer Roast syncs curves from one).
export const DROPBOX_SCOPES = 'files.content.write files.content.read files.metadata.read';
export function dropboxAuthorizeUrl(appKey) {
  if (!APP_CREDENTIAL.test(appKey)) throw new Error('Enter the App key from the Dropbox App Console.');
  return `https://www.dropbox.com/oauth2/authorize?client_id=${appKey}&response_type=code&token_access_type=offline&scope=${encodeURIComponent(DROPBOX_SCOPES)}`;
}
export function dropboxPacket(op, appKey = '', appSecret = '', code = '', id = requestId()) {
  if (!Number.isInteger(id) || id <= 0 || id > 0xffffffff || ![1, 2, 3].includes(op)) throw new Error('Invalid Dropbox request.');
  if (op === 1) {
    if (!APP_CREDENTIAL.test(appKey)) throw new Error('Enter the App key from the Dropbox App Console.');
    if (!APP_CREDENTIAL.test(appSecret)) throw new Error('Enter the App secret from the Dropbox App Console.');
    if (!/^[A-Za-z0-9_-]{1,128}$/.test(code)) throw new Error('Paste the authorization code Dropbox showed after you allowed access.');
  } else if (appKey || appSecret || code) throw new Error('This action does not accept credentials.');
  const key = encoder.encode(appKey), secret = encoder.encode(appSecret), grant = encoder.encode(code);
  const packet = new Uint8Array(9 + key.length + secret.length + grant.length);
  packet.set([2, op, key.length, secret.length]);new DataView(packet.buffer).setUint32(4, id, true);packet[8] = grant.length;
  packet.set(key, 9);packet.set(secret, 9 + key.length);packet.set(grant, 9 + key.length + secret.length);secret.fill(0);grant.fill(0);return packet;
}
export function decodeDropboxStatus(value) {
  const v = JSON.parse(new TextDecoder('utf-8', {fatal: true}).decode(value));
  if (v.v !== 2 || !['unconfigured', 'saved', 'authorizing', 'verifying', 'connected', 'failed'].includes(v.state) ||
      typeof v.saved !== 'boolean' || !Number.isInteger(v.request) || v.request < 0 || v.request > 0xffffffff ||
      typeof v.error !== 'string') throw new Error('The watch returned an unsupported Dropbox status.');
  if (v.state === 'connected' && !v.saved) throw new Error('Dropbox verification is incomplete.');
  return v;
}
export function decodeStatus(value) {
  const status = JSON.parse(new TextDecoder('utf-8', {fatal: true}).decode(value));
  if (status.v !== 1 || !['unconfigured', 'connecting', 'connected', 'failed', 'disconnected'].includes(status.state) ||
      typeof status.ssid !== 'string' || typeof status.ip !== 'string' || typeof status.saved !== 'boolean' ||
      !Number.isInteger(status.reason) || !Number.isInteger(status.request) || status.request < 0 || status.request > 0xffffffff) throw new Error('The watch returned an unsupported status.');
  if (status.state === 'connected' && !/^\d{1,3}(\.\d{1,3}){3}$/.test(status.ip)) throw new Error('The watch has not confirmed an IP address.');
  return status;
}
export class WatchConnection extends EventTarget {
  constructor(bluetooth) { super();this.bluetooth = bluetooth;this.queue = Promise.resolve();this.epoch = 0; }
  emit(type, detail) { this.dispatchEvent(new CustomEvent(type, {detail})); }
  operation(fn) { const result = this.queue.then(fn);this.queue = result.catch(() => {});return result; }
  async connect() {
    if (this.device) this.disconnect();
    const epoch = ++this.epoch;
    const device = await this.bluetooth.requestDevice({filters: [{services: [SERVICE]}]});
    if (epoch !== this.epoch) throw new Error('Connection cancelled.');
    this.device = device;
    device.addEventListener('gattserverdisconnected', () => {
      if (epoch === this.epoch) {this.epoch++;this.pendingRequest = 0;this.pendingDropboxRequest = 0;this.queue = Promise.resolve();this.device = null;this.status = null;this.config = null;this.dropboxConfig = null;this.dropboxStatus = null;this.emit('disconnected');}
    }, {once: true});
    let step = 'Connecting to watch';
    try {
      this.emit('progress', step);
      const server = await device.gatt.connect();
      step = 'Discovering Wi-Fi service';this.emit('progress', step);
      const service = await server.getPrimaryService(SERVICE);
      const config = await service.getCharacteristic(CONFIG);
      const status = await service.getCharacteristic(STATUS);
      const signal = await service.getCharacteristic(SIGNAL);
      const dropboxConfig = await service.getCharacteristic(DROPBOX_CONFIG);
      const dropboxStatus = await service.getCharacteristic(DROPBOX_STATUS);
      if (epoch !== this.epoch) throw new Error('Connection cancelled.');
      this.config = config;this.status = status;this.dropboxConfig = dropboxConfig;this.dropboxStatus = dropboxStatus;
      // This read requests authenticated encryption. Enter the watch's code
      // in the operating system's pairing dialog, never in this website.
      step = 'Authenticating setup';this.emit('progress', step);
      await this.readStatus(epoch);
      if (epoch !== this.epoch) throw new Error('Connection cancelled.');
      // Notifications have their own characteristic: readValue results can
      // never be replaced by an invalidation byte in the platform BLE cache.
      signal.addEventListener('characteristicvaluechanged', () => {
        if (epoch === this.epoch) this.readAll(epoch).catch(error => this.emit('error', error));
      });
      await this.operation(() => {if (epoch !== this.epoch) throw new Error('Connection cancelled.');return signal.startNotifications();});
      // Snapshot after subscription closes the read/subscribe event boundary.
      await this.readAll(epoch);this.emit('connected', device.name || 'Coffee Watch');
    } catch (error) {if (epoch === this.epoch) this.disconnect();throw new Error(`${step}: ${error.message}`);}
  }
  async readStatus(epoch = this.epoch) {
    return this.operation(async () => {
      if (epoch !== this.epoch || !this.status) return;
      const data = await this.status.readValue();
      if (epoch === this.epoch) {
        const status = decodeStatus(data);
        if (this.pendingRequest && status.request !== this.pendingRequest) return;
        if (status.request === this.pendingRequest && status.state !== 'connecting') this.pendingRequest = 0;
        this.emit('status', status);
      }
    });
  }
  async readAll(epoch = this.epoch) {await this.readStatus(epoch);await this.readDropboxStatus(epoch);}
  async readDropboxStatus(epoch = this.epoch) {
    return this.operation(async () => {
      if (epoch !== this.epoch || !this.dropboxStatus) return;
      const data = await this.dropboxStatus.readValue();
      if (epoch !== this.epoch) return;
      const value = decodeDropboxStatus(data);
      if (this.pendingDropboxRequest && value.request !== this.pendingDropboxRequest) return;
      if (!['authorizing', 'verifying'].includes(value.state)) this.pendingDropboxRequest = 0;
      this.emit('dropbox-status', value);
    });
  }
  async writeDropbox(packet) {
    const epoch = this.epoch;this.pendingDropboxRequest = new DataView(packet.buffer, packet.byteOffset, packet.byteLength).getUint32(4, true);
    try {
      await this.operation(async () => {
        if (epoch !== this.epoch || !this.dropboxConfig || !this.device?.gatt.connected) throw new Error('Connect to your watch first.');
        await this.dropboxConfig.writeValueWithResponse(packet);
      });
      if (epoch !== this.epoch) throw new Error('The watch disconnected before the request completed.');
      await this.readDropboxStatus(epoch);
    } catch (error) {if (epoch === this.epoch) this.pendingDropboxRequest = 0;throw error;} finally {packet.fill(0);}
  }
  async write(packet) {
    const epoch = this.epoch;this.pendingRequest = new DataView(packet.buffer, packet.byteOffset, packet.byteLength).getUint32(4, true);
    try {
      await this.operation(async () => {
        if (epoch !== this.epoch || !this.config || !this.device?.gatt.connected) throw new Error('Connect to your watch first.');
        await this.config.writeValueWithResponse(packet);
      });
      if (epoch !== this.epoch) throw new Error('The watch disconnected before the request completed.');
      // Write acceptance is not Wi-Fi success. A fresh device status is required.
      await this.readStatus(epoch);
    } catch (error) {if (epoch === this.epoch) this.pendingRequest = 0;throw error;} finally {packet.fill(0);}
  }
  disconnect() {
    ++this.epoch;this.pendingRequest = 0;this.pendingDropboxRequest = 0;this.queue = Promise.resolve();const device = this.device;this.device = null;this.status = null;this.config = null;this.dropboxConfig = null;this.dropboxStatus = null;device?.gatt.disconnect();
  }
}
