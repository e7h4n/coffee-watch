import {WatchConnection, joinPacket, forgetPacket} from './protocol.mjs';
const $ = id => document.getElementById(id);
const connect = $('connect'), fields = $('network-fields'), badge = $('connection-badge');
const watch = new WatchConnection(navigator.bluetooth);
let paired = false, busy = false;
function error(message = '') {$('error').textContent = message;$('error').hidden = !message;}
function status(title, detail, kind = '') {
  $('status-title').textContent = title;$('status-detail').textContent = detail;
  $('network-status').className = `network-status ${kind}`;
}
function controls() {fields.disabled = !paired || busy;$('forget').disabled = !paired || busy;}
function detached() {
  paired = false;busy = false;controls();badge.textContent = 'Not connected';badge.className = 'badge';
  connect.textContent = 'Connect watch ↗';connect.classList.remove('secondary');connect.disabled = false;
  $('device-name').textContent = 'Ready when your watch is.';$('password').value = '';
}
watch.addEventListener('connected', e => {
  paired = true;connect.textContent = 'Disconnect';connect.classList.add('secondary');
  badge.textContent = 'Paired';badge.className = 'badge connected';$('device-name').textContent = e.detail;controls();
});
watch.addEventListener('disconnected', () => {
  detached();status('Setup connection closed', 'Check the watch for network status. Open setup and connect again to make changes.');
});
watch.addEventListener('error', e => error(e.detail.message));
watch.addEventListener('status', e => {
  const v = e.detail;busy = v.state === 'connecting';controls();$('forget').hidden = !v.saved;
  if (v.state === 'connected') {
    status('Wi-Fi connected', `${v.ssid} · ${v.ip}${v.saved ? ' · Saved on watch' : ' · Not saved; device storage error'}`, v.saved ? 'success' : 'problem');
  } else if (v.state === 'connecting') status('Connecting to Wi-Fi', `The watch is joining ${v.ssid}. Waiting for its IP address.`);
  else if (v.state === 'failed' || v.state === 'disconnected') {
    const reasons = {201: 'Network not found. Check the name and use a 2.4 GHz network.', 202: 'Authentication failed. Check your password.', 204: 'The security handshake failed. Check your password.', 15: 'The security handshake timed out. Check your password.'};
    status(v.state === 'failed' ? 'Could not connect' : 'Wi-Fi disconnected', reasons[v.reason] || `The watch reported reason ${v.reason}. Enter your network details to connect again.`, 'problem');
  } else status('Ready for your network', 'Enter the network name exactly as it appears, including spaces.');
});
connect.addEventListener('click', async () => {
  error();
  if (paired) {watch.disconnect();detached();status('Setup connection closed', 'Check the watch for network status. Closing Bluetooth does not cancel a network request.');return;}
  connect.disabled = true;connect.textContent = 'Choose your watch…';
  status('Connect and pair', 'Keep Wi-Fi setup open on the watch. Enter its code in the system pairing dialog if asked.');
  try {await watch.connect();}
  catch (e) {detached();error(e.name === 'NotFoundError' ? 'No watch selected. Open Wi-Fi setup on the watch and try Connect watch again.' : `${e.message} Keep the setup screen open and complete the system pairing dialog.`);}
  finally {connect.disabled = false;}
});
$('show-password').addEventListener('click', () => {
  const shown = $('password').type === 'password';$('password').type = shown ? 'text' : 'password';
  $('show-password').textContent = shown ? 'Hide' : 'Show';$('show-password').setAttribute('aria-pressed', String(shown));
});
$('network-form').addEventListener('submit', async e => {
  e.preventDefault();error();let packet;
  try {packet = joinPacket($('ssid').value, $('password').value);}
  catch (e) {error(e.message);return;}
  $('password').value = '';$('password').type = 'password';$('show-password').textContent = 'Show';$('show-password').setAttribute('aria-pressed', 'false');
  busy = true;controls();status('Sending network details', 'Connection is confirmed only when the watch reports an IP address.');
  try {await watch.write(packet);}
  catch (e) {busy = false;controls();error(e.message);}
});
$('forget').addEventListener('click', async () => {
  if (!window.confirm('Forget the Wi-Fi network saved on your watch?')) return;
  error();busy = true;controls();
  try {await watch.write(forgetPacket());}
  catch (e) {error(e.message);}
  finally {busy = false;controls();}
});
if (!window.isSecureContext || !navigator.bluetooth) {
  connect.disabled = true;
  $('compatibility').textContent = 'Web Bluetooth is unavailable here. Open this HTTPS page in a supported Chrome browser on a Bluetooth-enabled computer or Android device.';
}
