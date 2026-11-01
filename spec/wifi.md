# Wi-Fi setup

The idle screen's bottom **Wi-Fi setup** button opens a modal settings screen.
Touch or Select opens it; Close, Back or Home closes it. No setup entry is added
to brewing, weighing or reporting. If the scale connects while setup is open,
the modal stays visible; closing it reveals the current normal workflow.

## Browser workflow

Open https://e7h4n.github.io/coffee-watch/ in a supported Chrome browser. Press
Connect watch and choose the device advertising `CoffeeWatch-XXXX`. Complete the
operating system's pairing dialog with the six-digit code shown on the watch,
if requested. Enter the exact 2.4 GHz SSID and password. An empty password means
an open network; secured networks use WPA2/WPA3 Personal. Enterprise Wi-Fi and
5 GHz are outside the supported interface.

The page is static, buildless HTML/CSS/ES modules. GitHub Pages serves the files;
there is no credentials backend, external script, analytics, local storage or
service worker. Passwords are sent only over authenticated, encrypted BLE. The
browser does not save them. A successful DHCP event stores credentials in the
watch's NVS `cw_wifi` namespace; they are not logged or returned over GATT. This
is normal device flash storage, not a claim of encrypted NVS or secure boot.
Forget saved network explicitly erases that namespace and disconnects Wi-Fi.

## BLE interface v1

| Purpose | UUID | Properties |
| --- | --- | --- |
| Service | c9a81001-9a9d-4f55-a621-5b4e7699b647 | Advertised only while setup is open |
| Configuration | c9a81002-9a9d-4f55-a621-5b4e7699b647 | Authenticated encrypted write with response |
| Status | c9a81003-9a9d-4f55-a621-5b4e7699b647 | Authenticated encrypted read / notify |

The watch is a temporary peripheral in addition to its normal scale/RS central
role. NimBLE supports three connections and LE Secure Connections with MITM
passkey pairing, display-only input capability, and no persistent bonds. A new
code is generated for each setup opening or browser disconnect. Closing setup
stops advertising and terminates its browser connection; Wi-Fi remains running.
Scale/RS connections are not owned by the provisioning GATT server.

Configuration bytes:

```text
0: version = 1
1: operation = 1 (join) or 2 (forget)
2: SSID UTF-8 byte length, 1..32 for join
3: password byte length, 0 or 8..63; 64 requires a hexadecimal PSK
4..7: nonzero request ID, uint32 little endian
8..: SSID bytes followed by password bytes, neither NUL terminated
```

Forget has zero SSID/password lengths and exactly eight bytes. Max join length
is 104 bytes. Invalid lengths, NULs, malformed SSID UTF-8, invalid operations and
zero request IDs are rejected. Configuration is write-only, never readable.

A status read returns JSON with `v`, `state`, `ssid`, `ip`, `reason`, `request`
and `saved`. States: unconfigured, connecting, connected, failed, disconnected.
Each notification is a one-byte invalidation signal (the current state code);
the browser serializes a fresh read after that notification. Native readValue
also emits characteristicvaluechanged, so JSON read values must not trigger more
reads. There is no status polling or automatic retry. Old request IDs cannot
confirm the current configuration operation.

## Device state

One network task serializes Wi-Fi operations. If replacing an active network,
it waits for the real disconnect event before applying the pending credentials.
Only IP_EVENT_STA_GOT_IP confirms connection and commits credentials. ATT write
success only accepts a configuration packet. Failure leaves a previously saved
network intact; it never silently reconnects to an alternate network. Credentials
are loaded on boot and one connection attempt is made. Disconnect/failure stays
visible until the user explicitly submits network details again or reboots.
No application retry loop, elapsed-time completion, SoftAP fallback or periodic
connection check is present. Wi-Fi events do not change settled brew data.

Network/setup state changes count as activity for the existing 10-minute idle
shutdown; periodic battery sampling, BLE status reads and rendering do not.
The factory partition is 4 MiB to accommodate the Wi-Fi driver and font assets;
NVS and PHY offsets are unchanged. OTA is not added by this feature.

## Development

```sh
python3 -m unittest discover -s tools -v
node --test web/protocol.test.mjs
python3 -m http.server 8086 --directory web
```

`localhost` is a browser secure-context exception for development. Production
uses HTTPS on GitHub Pages. `.github/workflows/pages.yml` deploys `web/` after
changes on main. No SSID or password should ever be added to source control.
