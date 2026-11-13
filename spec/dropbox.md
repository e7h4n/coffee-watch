# Dropbox authorization and uploads

## Setup

Records and probe traces are stored in the user's own Dropbox app folder. In the
[Dropbox App Console](https://www.dropbox.com/developers/apps), create an app
with **Scoped access** and **App folder** access. Under **Permissions**, enable
`files.content.write`, `files.content.read` and `files.metadata.read` and press **Submit**. Copy the
**App key** and **App secret** from **Settings**.

On the setup page, connect the watch over authenticated BLE, connect its Wi-Fi,
then enter the App key and App secret. The page builds Dropbox's authorization
link from the public App key alone:

```text
https://www.dropbox.com/oauth2/authorize?client_id=<app key>&response_type=code&token_access_type=offline&scope=files.content.write%20files.content.read%20files.metadata.read
```

The explicit `scope` requests only these file scopes (`files.metadata.read` lets a
device list folders in its app folder; Cardputer Roast uses the same page). Without it Dropbox asks
for every scope enabled on the app, and any team scope (for example
`team_data.member`) makes the page demand a work account.

There is no `redirect_uri`, so after the user allows access Dropbox displays an
authorization code on its own page. The user pastes it into the setup page and
presses **Connect Dropbox**. The browser sends the key, secret and code to the
watch and clears its inputs. Codes work once and expire within minutes.

Generated access tokens from the App Console are deliberately not used: Dropbox
issues only short-lived (four hour) access tokens to new apps. `offline` access
returns a refresh token that keeps working until the app is disconnected.

No credential or token is built into the published source, website or firmware.
The page uses no storage, external scripts, OAuth SDK or backend. Its CSP
forbids network requests (`connect-src 'none'`) and form submissions; Bluetooth
is the sole credential transport, and the authorization link is an ordinary
navigation. Password-manager behavior is a browser setting.

The watch stores the App key, App secret and refresh token together in its
`cw_dropbox` NVS namespace after a valid token exchange. NVS is ordinary device
flash storage; encrypted NVS and secure boot are not provided, so device
possession can expose stored credentials. Access tokens and the authorization
code are transient. Credentials, token responses and authorization headers are
not logged or returned over GATT; private buffers are cleared after use.

## One authorization path

1. Synchronize the clock with SNTP (`pool.ntp.org`) if needed. HTTPS uses the
   ESP-IDF certificate bundle, validates certificates and does not follow
   redirects.
2. POST the code, `grant_type=authorization_code`, client ID and client secret to
   `https://api.dropboxapi.com/oauth2/token`.
3. Require token type `bearer`, a positive `expires_in`, an access token, a
   refresh token and the `files.content.write` scope. Errors are reported as
   `code_rejected` (`invalid_grant`), `client_rejected` (`invalid_client`),
   `no_refresh_token`, `missing_scope` or `authorization_rejected`.
4. Persist the account, then verify an actual upload (below).

Each upload or check exchanges the refresh token for a fresh access token
(`grant_type=refresh_token`). A failed exchange is `refresh_failed`. There is no
automatic retry, alternative authorization method, periodic health check or
browser token exchange. A new explicit action starts a new request; request IDs
prevent an old snapshot from acknowledging a new command.

## Uploads and verification

Uploads use `https://content.dropboxapi.com/2/files/upload` with a
`Dropbox-API-Arg` of `{"path","mode","autorename":false,"mute":true,
"strict_conflict":true}`. Paths are relative to the app folder
(Dropbox › Apps › *app name*). After every upload the watch compares Dropbox's
`content_hash` of the stored file with its own (SHA-256 of the concatenated
SHA-256 digests of each 4 MiB block) and checks the returned path. Only a match
counts as saved; this is an exact content check without a download.

**Check Dropbox upload** writes `/connection-check.json` with mode `overwrite`
and verifies its hash; only that reports **Dropbox upload verified**. After
reboot the UI reports **Authorization saved on watch**, not a live connection.

**Remove Dropbox from watch** erases the local namespace and leaves Dropbox files
intact. Revoke access by disconnecting the app under Connected apps in Dropbox
settings. Wi-Fi and brew state are separate.

## BLE interface

The setup service is `c9a81001-9a9d-4f55-a621-5b4e7699b647`.

| Characteristic | UUID suffix | Properties |
| --- | --- | --- |
| Dropbox command | `1005` | Authenticated encrypted write with response |
| Dropbox status | `1006` | Authenticated encrypted read |
| State changed | `1004` | Existing notification-only invalidation signal |

The suffix replaces `1001` in the service UUID. One signal causes serialized
fresh reads of both Wi-Fi and Dropbox status; notifications never replace JSON
in read characteristic caches. Command packet, maximum 201 bytes:

```text
0       version = 2
1       operation = 1 authorize, 2 local removal, 3 upload check
2       App key ASCII length, max 32
3       App secret ASCII length, max 32
4..7    nonzero request ID, uint32 little endian
8       authorization code ASCII length, max 128
9..     App key, App secret, then code, without NUL terminators
```

Authorize requires all three; the key and secret are letters and digits, the
code letters, digits, `-` and `_`. Removal and check carry no credentials and are
exactly nine bytes. Malformed lengths, zero request IDs and unknown operations
are rejected.

Status JSON fields: `v` (2), `state`, `request`, `saved`, `error`. States:
`unconfigured`, `saved`, `authorizing`, `verifying`, `connected`, `failed`.
Neither the App secret, a token nor the code is in the public schema.

## References

- [OAuth guide](https://developers.dropbox.com/oauth-guide)
- [Authorization endpoints](https://www.dropbox.com/developers/documentation/http/documentation#authorization)
- [files/upload](https://www.dropbox.com/developers/documentation/http/documentation#files-upload)
- [Content hash](https://www.dropbox.com/developers/reference/content-hash)
