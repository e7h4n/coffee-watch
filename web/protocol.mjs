// SPDX-License-Identifier: MIT
export const SERVICE = 'c9a81001-9a9d-4f55-a621-5b4e7699b647';
export const CONFIG = 'c9a81002-9a9d-4f55-a621-5b4e7699b647';
export const STATUS = 'c9a81003-9a9d-4f55-a621-5b4e7699b647';
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
      if (epoch === this.epoch) {this.epoch++;this.pendingRequest = 0;this.queue = Promise.resolve();this.device = null;this.status = null;this.config = null;this.emit('disconnected');}
    }, {once: true});
    try {
      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(SERVICE);
      const config = await service.getCharacteristic(CONFIG);
      const status = await service.getCharacteristic(STATUS);
      if (epoch !== this.epoch) throw new Error('Connection cancelled.');
      this.config = config;this.status = status;
      // This read requests authenticated encryption. Enter the watch's code
      // in the operating system's pairing dialog, never in this website.
      await this.readStatus(epoch);
      if (epoch !== this.epoch) throw new Error('Connection cancelled.');
      this.status.addEventListener('characteristicvaluechanged', event => {
        // Native readValue() also emits this event. Only a one-byte notification
        // is an invalidation signal; JSON read results must never trigger reads.
        if (epoch === this.epoch && event.target.value.byteLength === 1) this.readStatus(epoch).catch(error => this.emit('error', error));
      });
      await this.operation(() => {if (epoch !== this.epoch) throw new Error('Connection cancelled.');return status.startNotifications();});
      // Snapshot after subscription closes the read/subscribe event boundary.
      await this.readStatus(epoch);this.emit('connected', device.name || 'Coffee Watch');
    } catch (error) {if (epoch === this.epoch) this.disconnect();throw error;}
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
    ++this.epoch;this.pendingRequest = 0;this.queue = Promise.resolve();const device = this.device;this.device = null;this.status = null;this.config = null;device?.gatt.disconnect();
  }
}
