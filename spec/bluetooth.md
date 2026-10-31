# Bluetooth interfaces

Supported baseline: BOOKOO Themis Ultra 4.0.1, Ratio + Auto; DiFluid RS v002-005.
Transport is NimBLE central with two connections. The demo targets the two known
peer addresses configured in transport.c; it has no device-picker UI.

## BOOKOO

Service 0FFE, command/event characteristic FF12, weight/dose characteristic FF11.
Subscribe to both. Commands are six bytes: `03 0A opcode param1 param2 XOR`.
Notifications are 20 bytes with XOR validation. Standard 16-bit Bluetooth UUIDs.

| Action | Command | Confirmation |
| --- | --- | --- |
| Stored dose query, once after subscription | `03 0A 14 00 00 1D` | `03 10`, unsigned BE24 centigrams at offsets 6..8 |
| Enter Auto Ready | `03 0A 18 00 00 11` | `03 0D`, state 02 |
| Exit Ready | `03 0A 18 01 00 10` | `03 0D`, state 03 |
| Stop | opcode 05, params zero | `03 0D`, state 00 |
| Tare in idle | opcode 01, params zero | Dispatch only; no independent completion event |
| Exit Done and tare | opcode 07, params zero | State 04 exits Done; not a synthetic tare ACK |

`03 0F` reports dose changes (ASCII sign, BE24 centigrams). `03 0B` reports live
weight. Auto states: 02 Ready, 01 Start, 00 Stop, 03 Exit Ready, 04 Exit Done.
Ready/Start can carry old elapsed time; do not treat it as a new timer result.
Freeze Stop's water/time independently of the ordinary weight stream.

In Auto Done, the live stream is zero with the server present and negative gross
mass after removal, even while the scale display retains its result. Use that
negative gross mass minus the previously captured empty-server mass for beverage.

The session adapter keeps a scale-reported dose cache only within the current
connection. Start/result dose snapshots are independent. ATT write success does
not confirm Ready, Start or Stop. Each action has one request and one exact event.

Interface references: [official Ultra protocol](https://github.com/BooKooCode/OpenSource/blob/main/bookoo_ultra_scale/protocols.md)
and [official BOOKOO client](https://bookoocoffee.com/pages/app-download), BOOKOO N
3.8.2 for stored feature query and Auto Ready commands.

## DiFluid RS

Service 00FA, result characteristic FF01. This is RS, not the browser prototype's
R2 placeholder. Do not use AA01 legacy R2 commands. Subscribe only for a fresh
measurement after beverage capture; no calibration, OTA or remote-start commands.

Frames have DA DA / 0A 0A boundaries and FA escaping for DA, 0A, FA. Unescaped
body is ciphertext, level byte and sum8. Level low two bits must equal 1.
AES-128-CBC key `7a3fe91c8b5d2a6e4c91f738b264d50a`, IV
`9e2b7c451f8a3d6be457920c8f3a6db1`, as used by CoffeeOS 1.1.25.
Plaintext route must be `01 03 12 00`, followed by LE16 payload length, payload
and CRC16-LE (initial FFFF, polynomial A001). Ignore padding outside declared
length. The first 30 payload bytes follow `<BffBfIfff`: error, Brix, averaged Brix,
count, temperature, error flags, refraction, glue, liquid. Extra payload bytes are
not interpreted. Nonzero error or flags produces an error, never a valid result.

The decoder supports fragmented and adjacent frames with bounded storage. A
connection change discards partial frames. CRC, sum, level and exact result route
must all validate before reporting. Native and Python tests retain representative
packet fixtures; no APK or decompiled client source is included.
