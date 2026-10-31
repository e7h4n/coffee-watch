# Bluetooth integration notes

Verified against official public documents on 2026-10-31. These are **documented capabilities**, not proof of compatibility with the user's two physical units. UI prototypes use simulated packets/events.

## BOOKOO Themis Ultra

[Official Ultra protocol](https://github.com/BooKooCode/OpenSource/blob/main/bookoo_ultra_scale/protocols.md), document updated September 18, 2026.

- BLE service: `0x0FFE`.
- Command characteristic: `0xFF12`.
- Weight notification characteristic: `0xFF11`.
- UUID expansion: `0000xxxx-0000-1000-8000-00805F9B34FB`.
- Command envelope: `03 0A COMMAND DATA1 DATA2 XOR`.
- Checksum: XOR of preceding bytes.

### Documented commands

| Code | Action | Important constraint |
| --- | --- | --- |
| `01` | Tare | Not available during automatic running/done or charging; exact supported phases depend on firmware |
| `02` | Beep level | Release 4.0.0+ accepts 0–3 |
| `03` | Auto-off time | 5–30 minutes |
| `04` | Start timer | Timing/ratio mode |
| `05` | Stop timer | Timing/ratio mode |
| `06` | Reset timer | Timing/ratio mode |
| `07` | Tare and start | Phase-dependent behavior; not a universal start command |
| `08` | Flow smoothing | Enable/disable |
| `09` | Calibration | Mode support varies by firmware; not part of normal brew flow |
| `0B` | Automatic stop condition | Parameter layout differs between releases ≤3.1.2 and ≥4.0.0; do not interchange |
| `0D` | Set dose | 0.1–999.9g; beta ≥3.2.4 or release ≥4.0.0 |
| `15` | Shutdown | Release ≥4.0.0; ignored while charging |
| `25` | Refresh auto-off countdown | Release ≥4.0.1 |

### Notifications

- `03 0B`: time in milliseconds, signed weight and flow, battery, unit/settings. Weight values are in grams regardless of display-unit setting. Weight representation is hundredths of a gram, not an accuracy guarantee.
- `03 0F`: signed coffee dose; beta ≥3.2.4 or release ≥4.0.0.
- `03 0D`: automatic mode ready/start/stop/exit events plus elapsed time, weight and mode-dependent settlement data; same version floor as dose notifications.
- Settlement ratio represents liquid weight / dose in ratio mode; do not assume it always represents our added-water ratio without validating the scale's use mode.

### Product integration requirements

1. Subscribe and validate packets/checksums before considering the scale ready.
2. Obtain a valid dose packet rather than deriving dose from the entire loaded assembly.
3. Map UI tare/start/stop commands to observed scale results. UI transitions must follow confirmed state, not optimistic command dispatch alone.
4. Freeze brew time, dose and water at the stop event; subsequent tare/object-removal readings are for beverage capture only.
5. Initial auto-zero is a user expectation; verify near-zero actual readings. A BLE connection alone does not document an automatic tare.
6. Physical reset-key events are not separately exposed in the public contract. Detect appropriate near-zero readings within the current state and/or confirm a sent tare command.
7. Manual timer start/stop state notifications are not separately documented like automatic events. Validate the user's firmware and mode before promising reliable externally initiated start/stop detection.
8. Do not confuse a constant timer, disconnected stream or pour pause with a finished brew.
9. Prefer a validated end condition such as filter removal rather than flow stopping in multi-pour brewing.
10. Hardware mode selection and firmware-version read commands are not included in this official public command table. Community discoveries must be separately validated before use.

## DiFluid R2 Extract

[Official R2 protocol](https://github.com/DiFluid/difluid-sdk-demo/blob/master/docs/protocolR2.md), v1.0.0, March 18, 2023; minimum firmware 2.1.2.

[SDK readme](https://github.com/DiFluid/difluid-sdk-demo) states encrypted and cleartext channels are now enabled by default; the cloud relay step is optional. Do not introduce a mandatory cloud dependency for normal measurements. The readme's channel terminology must be checked against actual GATT discovery when implementing.

- Packet envelope: `DF DF FUNC CMD LENGTH DATA... CHECKSUM`.
- Checksum: sum of all previous bytes modulo 256.
- Results are split across numbered packets; assemble and validate the complete current measurement.

### Capabilities

| Category | Documented functions |
| --- | --- |
| Identity | Serial number, model, firmware version |
| Settings | Read/set temperature unit, automatic testing, screen brightness, offline test count |
| Measurement | Trigger one test; trigger average test with a supplied count |
| Result | TDS, refractive index, prism temperature, sample-tank temperature |
| Average result | Average TDS/index/temperatures, current and total test counts |
| Status | Test start/finish, calibration start/finish, average start/progress/finish, loop start/progress/finish |
| Calibration | Trigger and receive status/result |
| Errors | Test failure, calibration failure, no liquid, beyond range, hardware errors |

### Temperature waiting

The protocol explicitly describes loop-test responses while temperature is unstable and average-test behavior when multiple tests are selected. It also documents long-running notifications that **do not contain valid measurement results**.

- Do not invent an exact remaining wait time.
- Do not show a final report until a fresh valid result is available.
- Never accept a calibration zero as coffee TDS.
- The visible baseline remains `Waiting TDS`; richer temperature copy has not yet been specified.
- Validate the exact high-temperature behavior on the user's firmware.

### Not documented as available in this public protocol

Battery readout, historical record export, direct extraction-yield computation and production-ready third-party OTA are not established by this document. SDK OTA is described as work in progress. Do not promise these features.

## Hardware validation checklist

- Record the actual scale and R2 firmware versions and discovered GATT services/characteristics.
- Confirm dose notifications in the chosen scale mode and persistence across repeated tare.
- Confirm physical Start/Stop and BLE-issued Start/Stop behavior in that mode.
- Confirm scale tare restrictions, acknowledgement/near-zero handling and connection auto-zero behavior.
- Confirm simultaneous or sequential BLE connections with StopWatch, plus reconnection behavior.
- Confirm real weight-stream rate and tune the 0.2g/5s stability algorithm.
- Confirm R2 measurement identity, packet order, high-temperature status, errors and final result.
- Check protocol documentation inconsistencies against packet examples and device captures, particularly multi-byte result fields.
- Do not allow missing/zero dose, invalid TDS or stale data to generate a misleading ratio/extraction/chart.
