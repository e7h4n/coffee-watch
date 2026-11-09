import {WatchConnection, joinPacket, forgetPacket, dropboxPacket, dropboxAuthorizeUrl} from './protocol.mjs';
const $ = id => document.getElementById(id);
const connect = $('connect'), fields = $('network-fields'), badge = $('connection-badge');
const watch = new WatchConnection(navigator.bluetooth);
let paired = false, busy = false, wifiConnected = false, dropboxBusy = false, dropboxSaved = false;
function error(message = '') {$('error').textContent = message;$('error').hidden = !message;}
function status(title, detail, kind = '') {
  $('status-title').textContent = title;$('status-detail').textContent = detail;
  $('network-status').className = `network-status ${kind}`;
}
function controls() {
  fields.disabled = !paired || busy;$('forget').disabled = !paired || busy;
  $('dropbox-fields').disabled = !paired || !wifiConnected || dropboxBusy;
  $('dropbox-verify').disabled = !paired || !wifiConnected || !dropboxSaved || dropboxBusy;
  $('dropbox-forget').disabled = !paired || (!dropboxSaved && !dropboxBusy);
}
function detached() {
  paired = false;busy = false;wifiConnected = false;dropboxBusy = false;controls();badge.textContent = 'Not connected';badge.className = 'badge';
  connect.textContent = 'Connect watch ↗';connect.classList.remove('secondary');connect.disabled = false;
  $('device-name').textContent = 'Ready when your watch is.';$('password').value = '';clearDropboxInputs();
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

function clearDropboxInputs() {$('app-key').value = '';$('app-secret').value = '';$('auth-code').value = '';authorizeLink();}
function dropboxError(message = '') {$('dropbox-error').textContent = message;$('dropbox-error').hidden = !message;}
// The authorization link needs only the public App key; the secret never leaves the form except to the watch.
function authorizeLink() {
  try {$('dropbox-authorize').href = dropboxAuthorizeUrl($('app-key').value.trim());}
  catch {$('dropbox-authorize').removeAttribute('href');}
}
$('app-key').addEventListener('input', authorizeLink);
watch.addEventListener('dropbox-status', e => {
  const v = e.detail;dropboxSaved = v.saved;dropboxBusy = ['authorizing', 'verifying'].includes(v.state);controls();
  const titles = {unconfigured:'Dropbox not connected', saved:'Authorization saved on watch', authorizing:'Exchanging the code with Dropbox', verifying:'Checking a Dropbox upload', connected:'Dropbox upload verified', failed:'Dropbox setup needs attention'};
  $('dropbox-title').textContent = titles[v.state];
  const errors = {clock_sync_failed:'Could not synchronize device time. Check internet access before trying again.', dropbox_unreachable:'The watch could not reach Dropbox. Check its network access.', code_rejected:'Dropbox rejected the code. Codes work once and expire after a few minutes; get a new one and try again.', client_rejected:'Dropbox rejected the App key or App secret. Copy both again from the App Console.', authorization_rejected:'Dropbox rejected the authorization. Check the app settings and try again.', no_refresh_token:'Dropbox did not return a refresh token. Use the authorization link on this page to get the code.', missing_scope:'The app lacks files.content.write. Enable it under Permissions, press Submit, then get a new code.', invalid_token_response:'Dropbox returned an unexpected token response.', refresh_failed:'The saved authorization could not be refreshed. Check connectivity, or authorize again if the app was disconnected.', upload_check_failed:'Could not upload and verify connection-check.json. Check internet access and the app permissions.', storage_error:'Could not save device credentials.', authorize_first:'Authorize the watch first.'};
  $('dropbox-detail').textContent = v.state === 'failed' ? (errors[v.error] || 'The watch could not complete this request.') : v.state === 'connected' ? 'The watch uploaded connection-check.json and Dropbox’s content hash matched.' : v.state === 'saved' ? 'Use Check Dropbox upload to verify the saved authorization.' : v.state === 'authorizing' ? 'The watch is exchanging the code for a refresh token with Dropbox.' : v.state === 'verifying' ? 'Uploading connection-check.json to the app folder.' : 'Connect Wi-Fi, then enter your Dropbox app details.';
  $('dropbox-result').className = `network-status ${v.state === 'connected' ? 'success' : v.state === 'failed' ? 'problem' : ''}`;
});
$('dropbox-form').addEventListener('submit', async e => {
  e.preventDefault();dropboxError();let packet;
  try {packet = dropboxPacket(1, $('app-key').value.trim(), $('app-secret').value.trim(), $('auth-code').value.trim());}
  catch (e) {dropboxError(e.message);return;}
  clearDropboxInputs();dropboxBusy = true;controls();$('dropbox-title').textContent = 'Sending app details and code';
  try {await watch.writeDropbox(packet);}catch (e) {dropboxBusy = false;controls();dropboxError(e.message);}
});
$('dropbox-verify').addEventListener('click', async () => {
  dropboxError();dropboxBusy = true;controls();
  try {await watch.writeDropbox(dropboxPacket(3));}catch (e) {dropboxBusy = false;controls();dropboxError(e.message);}
});
$('dropbox-forget').addEventListener('click', async () => {
  if (!window.confirm('Erase Dropbox credentials from this watch? Existing Dropbox files remain. Disconnect the app in your Dropbox settings to revoke access.')) return;
  dropboxError();clearDropboxInputs();
  try {await watch.writeDropbox(dropboxPacket(2));}catch (e) {dropboxError(e.message);}
});
window.addEventListener('pagehide', () => {clearDropboxInputs();$('password').value = '';watch.disconnect();});
