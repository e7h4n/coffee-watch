import {WatchConnection, joinPacket, forgetPacket, drivePacket} from './protocol.mjs';
const $ = id => document.getElementById(id);
const connect = $('connect'), fields = $('network-fields'), badge = $('connection-badge');
const watch = new WatchConnection(navigator.bluetooth);
let paired = false, busy = false, wifiConnected = false, driveBusy = false, driveSaved = false;
function error(message = '') {$('error').textContent = message;$('error').hidden = !message;}
function status(title, detail, kind = '') {
  $('status-title').textContent = title;$('status-detail').textContent = detail;
  $('network-status').className = `network-status ${kind}`;
}
function controls() {
  fields.disabled = !paired || busy;$('forget').disabled = !paired || busy;
  $('drive-fields').disabled = !paired || !wifiConnected || driveBusy;
  $('drive-verify').disabled = !paired || !wifiConnected || !driveSaved || driveBusy;
  $('drive-forget').disabled = !paired || (!driveSaved && !driveBusy);
}
function detached() {
  paired = false;busy = false;wifiConnected = false;driveBusy = false;controls();badge.textContent = 'Not connected';badge.className = 'badge';
  connect.textContent = 'Connect watch ↗';connect.classList.remove('secondary');connect.disabled = false;
  $('device-name').textContent = 'Ready when your watch is.';$('password').value = '';clearDriveInputs();$('drive-authorization').hidden = true;$('drive-code').textContent = '';$('drive-authorize').removeAttribute('href');
}
watch.addEventListener('connected', e => {
  paired = true;connect.textContent = 'Disconnect';connect.classList.add('secondary');
  badge.textContent = 'Paired';badge.className = 'badge connected';$('device-name').textContent = e.detail;controls();
});
watch.addEventListener('disconnected', () => {
  detached();status('Setup connection closed', 'Check the watch for network status. Open setup and connect again to make changes.');
});
watch.addEventListener('progress', e => {$('device-name').textContent = e.detail;badge.textContent = 'Connecting';});
watch.addEventListener('error', e => error(e.detail.message));
watch.addEventListener('status', e => {
  const v = e.detail;busy = v.state === 'connecting';wifiConnected = v.state === 'connected';controls();$('forget').hidden = !v.saved;
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

function clearDriveInputs() {$('client-id').value = '';$('client-secret').value = '';}
function driveError(message = '') {$('drive-error').textContent = message;$('drive-error').hidden = !message;}
watch.addEventListener('drive-status', e => {
  const v = e.detail;driveSaved = v.saved;driveBusy = ['requesting_code', 'awaiting_consent', 'verifying'].includes(v.state);controls();
  const titles = {unconfigured:'Drive not connected', saved:'Authorization saved on watch', requesting_code:'Requesting Google authorization', awaiting_consent:'Authorize your watch on Google', verifying:'Checking Drive read/write', connected:'Drive read/write verified', failed:'Drive setup needs attention'};
  $('drive-title').textContent = titles[v.state];
  const errors = {clock_sync_failed:'Could not synchronize device time. Check internet access before trying again.', google_unreachable:'The watch could not reach Google. Check its network access.', device_code_rejected:'Google rejected this client. Use a TVs and Limited Input devices client.', authorization_expired:'The code expired. Start authorization again.', access_denied:'Authorization was declined.', authorization_rejected:'Google rejected authorization. Check client settings and the test-user list.', refresh_failed:'The saved authorization could not be refreshed. Check connectivity or authorize again.', drive_verification_failed:'Could not verify the folder and connection-check.json. Check internet access and Drive permissions.', storage_error:'Could not save device credentials.', authorize_first:'Authorize the watch first.', invalid_token_response:'Google did not return the required Drive authorization.'};
  $('drive-detail').textContent = v.state === 'failed' ? (errors[v.error] || 'The watch could not complete this request.') : v.state === 'connected' ? 'The watch wrote connection-check.json and read it back successfully.' : v.state === 'saved' ? 'Use Check Drive read/write to verify the saved authorization.' : v.state === 'awaiting_consent' ? 'Use the code below. The watch receives its tokens directly from Google.' : v.state === 'verifying' ? 'Creating or checking the Coffee Watch folder and its connection file.' : v.state === 'unconfigured' ? 'Connect Wi-Fi, then enter your own OAuth client credentials.' : 'Waiting for Google to issue a device code.';
  $('drive-result').className = `network-status ${v.state === 'connected' ? 'success' : v.state === 'failed' ? 'problem' : ''}`;
  $('drive-authorization').hidden = v.state !== 'awaiting_consent';
  $('drive-code').textContent = v.state === 'awaiting_consent' ? v.user_code : '';
  if (v.state === 'awaiting_consent') $('drive-authorize').href = v.verification_url;else $('drive-authorize').removeAttribute('href');
  $('drive-folder').hidden = !v.folder;if (v.folder) $('drive-folder').href = `https://drive.google.com/drive/folders/${v.folder}`;else $('drive-folder').removeAttribute('href');
});
$('drive-form').addEventListener('submit', async e => {
  e.preventDefault();driveError();let packet;
  try {packet = drivePacket(1, $('client-id').value.trim(), $('client-secret').value);}
  catch (e) {driveError(e.message);return;}
  clearDriveInputs();driveBusy = true;controls();$('drive-title').textContent = 'Sending client credentials';
  try {await watch.writeDrive(packet);}catch (e) {driveBusy = false;controls();driveError(e.message);}
});
$('drive-verify').addEventListener('click', async () => {
  driveError();driveBusy = true;controls();
  try {await watch.writeDrive(drivePacket(3));}catch (e) {driveBusy = false;controls();driveError(e.message);}
});
$('drive-forget').addEventListener('click', async () => {
  if (!window.confirm('Cancel authorization and erase Drive credentials from this watch? Existing Drive files remain. Revoke access separately in your Google Account.')) return;
  driveError();clearDriveInputs();
  try {await watch.writeDrive(drivePacket(2));}catch (e) {driveError(e.message);}
});
window.addEventListener('pagehide', () => {clearDriveInputs();$('password').value = '';watch.disconnect();});
