# DiFluid RS event decoder

Portable C decoder for the user's RS v002-005, with protocol evidence from official
CoffeeOS 1.1.25 and a CRC-verified real physical measurement. The ESP-IDF demo
provides mbedTLS AES-128-CBC; the stream component owns no platform or BLE APIs.

Only service 00FA / characteristic FF01 is routed here. Each peer/connection has
its own stream; disconnect and host reset discard partial frames. The decoder
accepts arbitrary notification fragments and multiple consecutive frames,
with a bounded 512-byte unescaped body. Oversize frames produce explicit rejection.
The component never reads a clock, polls, retries, or fabricates completion.
A new DA DA boundary allows a subsequent frame after invalid input.

Exact result route: 01 03 12 00, after outer sum, level-1 AES, explicit payload
length and inner CRC16 validation. Other CRC-valid routes are unsupported, never
successful measurements. Nonzero device error_code or error_flags produces
RS_MEASUREMENT_ERROR. Values retain the device's float precision; display rounding
belongs to the UI. Additional payload bytes are counted, not interpreted.

The host CommonCrypto harness exercises the actual C decoder with the real AES
capture at every split point, one-byte fragments, adjacent frames, disconnect
mid-frame, corrupt transport and oversized frames. It is macOS-only; Python
protocol tests run independently. The demo uses ESP-IDF mbedTLS instead.

This component decodes results only. It does not expose measurement-start,
firmware-update, calibration, or old R2 AA01 commands. Complete brew association
and extraction-yield calculation are separate business work, not inferred from
an arriving result.
