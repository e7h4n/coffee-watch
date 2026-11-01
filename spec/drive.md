# Google Drive device authorization

## Setup

The static setup page accepts a user's own Google OAuth client ID and client
secret. Create a **TVs and Limited Input devices** client in Google Cloud,
enable Google Drive API, configure the consent screen, and declare only
`https://www.googleapis.com/auth/drive.file`. For an app in Testing, add the
Google account to its test-user list. Testing refresh tokens expire after seven
days; they are not permanent device keys.

Connect the watch over authenticated BLE, connect its Wi-Fi, enter the client
credentials, then press **Connect Google Drive**. The browser clears the inputs
after forming the command. It receives only public setup status: the user code,
Google verification URL, folder ID, error category and request ID. Follow the
Google link, enter the displayed code, and authorize on Google's own page.
The watch receives access/refresh tokens directly from Google over HTTPS.

No client credential or user token is built into the published source, website
or firmware. The page uses no storage, external scripts, OAuth SDK or backend.
Its CSP forbids network requests (`connect-src 'none'`) and form submissions;
Bluetooth is the sole credential transport. Password-manager behavior is a
browser setting, independent of the page's own storage policy.

The watch stores the OAuth client credentials, refresh token, folder and check
file IDs together in its `cw_drive` NVS namespace after a valid token response.
NVS is ordinary device flash storage; encrypted NVS and secure boot are not
provided. Device possession can expose stored credentials. Access tokens and
pending device codes remain transient and are cleared after the operation.
Credentials, token responses and authorization headers are not logged or
returned over GATT. Private JSON strings and command/form buffers are cleared
before release where supported; JavaScript strings cannot be reliably zeroized.

## One authorization path

1. On explicit authorization, synchronize the device clock using SNTP
   (`pool.ntp.org`) if needed. HTTPS uses the ESP-IDF certificate bundle,
   validates certificates and does not follow redirects.
2. Request a code from `https://oauth2.googleapis.com/device/code` with the
   client ID and `drive.file` scope.
3. Publish Google's user code and allowlisted HTTPS verification URL.
4. Poll `https://oauth2.googleapis.com/token` at the server's `interval`, until
   the server accepts, denies, rejects or expires the request. `slow_down`
   increases the interval by five seconds. This is OAuth device-flow protocol
   polling, not a status watchdog or a fallback completion mechanism.
5. Require a successful token response including `drive.file` and a refresh
   token. Persist it, resolve/create the device-marked Coffee Watch folder, and
   verify an actual file write followed by a read.

A bounded HTTPS or time-sync failure is reported. There is no automatic retry,
alternative authorization method, broad Drive scope, periodic health check or
browser token exchange. A new explicit action starts a new request. Request IDs
prevent an old snapshot from acknowledging a new command. Local removal cancels
an in-flight session; stale HTTP completions cannot publish its success.

Closing Bluetooth does not revoke an authorization: the device completes its
active Google flow until consent, failure or expiry. The global ten-minute idle
shutdown still applies. Public Drive state transitions count as activity;
authorization polling itself does not.

## Folder and read/write verification

`drive.file` grants access to files created by the application or explicitly
selected for it; it is not recursive access to arbitrary existing folder files.
The watch resolves its own folder by a device marker in `appProperties`, and
creates **Coffee Watch** if none exists. Multiple matching folders are an error.
It persists the returned folder ID and validates an existing folder's type and
trash status instead of silently selecting another one.

The initial check and **Check Drive read/write** create or update
`connection-check.json` inside that folder, then GET its content and compare it
with the expected JSON. Existing check files must have the same parent before
PATCH. Only the successful read-back reports **Drive read/write verified**.
ATT acceptance, token acquisition and folder creation alone are insufficient.

After reboot the UI reports **Authorization saved on watch**, not a claimed
live connection. An explicit check loads the credentials, obtains a fresh access
token, and performs the same write/read sequence. The browser provides a link
to the returned folder. Brew record upload is not enabled by this setup feature;
its record schema and trigger remain a separate product decision.

**Remove Drive from watch / cancel** erases the local namespace and leaves Drive
files intact. It does not revoke all project-level Google authorizations. Revoke
access in Google Account settings when desired. Wi-Fi and brew state are separate.

## BLE interface

The existing setup service is `c9a81001-9a9d-4f55-a621-5b4e7699b647`.

| Characteristic | UUID suffix | Properties |
| --- | --- | --- |
| Drive command | `1005` | Authenticated encrypted write with response |
| Drive status | `1006` | Authenticated encrypted read |
| State changed | `1004` | Existing notification-only invalidation signal |

The suffix replaces `1001` in the service UUID. One signal causes serialized
fresh reads of both Wi-Fi and Drive status; notifications never replace JSON in
read characteristic caches. Status snapshots remain coherent across Read Blob.
Drive command packet, maximum 232 bytes:

```text
0       version = 1
1       operation = 1 authorize, 2 local removal/cancel, 3 read/write check
2       client ID ASCII byte length, max 128
3       client secret ASCII byte length, max 96
4..7    nonzero request ID, uint32 little endian
8..     client ID, then secret, without NUL terminators
```

Authorize requires both fields; IDs end in `.apps.googleusercontent.com`.
Only ASCII letters, digits, dots, underscores and hyphens are accepted.
Removal/check must have zero credential lengths and exactly eight bytes.
Malformed lengths, zero request IDs and unknown operations are rejected.

Status JSON fields: `v`, `state`, `request`, `saved`, `error`, `user_code`,
`verification_url`, `folder`. States: `unconfigured`, `saved`, `requesting_code`,
`awaiting_consent`, `verifying`, `connected`, `failed`. Neither OAuth client
credential, access/refresh token nor `device_code` is in the public schema.

## References

- [Google device flow](https://developers.google.com/identity/protocols/oauth2/limited-input-device)
- [Drive scopes](https://developers.google.com/workspace/drive/api/guides/api-specific-auth)
- [Drive uploads](https://developers.google.com/workspace/drive/api/guides/manage-uploads)
- [Token expiration](https://developers.google.com/identity/protocols/oauth2#expiration)
