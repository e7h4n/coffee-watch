# Coffee Watch hardware demo

Native ESP32-S3 firmware for the physical M5Stack StopWatch, BOOKOO Themis Ultra
4.0.1 (Ratio + Auto), and DiFluid RS v002-005. All pages from the accepted browser
prototype are implemented: brewer carousel, empty-server capture, single-Start preparation and Auto Ready,
brewing, beverage capture, Waiting TDS, numerical report and Golden Cup chart.
The browser's R2 placeholder is replaced by the actual validated RS interface.
There is no simulated measurement or offline mode in this firmware.

## Build and flash

```sh
python3 fetch_drivers.py
# Activate ESP-IDF 5.5.x, then:
IDF_COMPONENT_MANAGER=0 idf.py build
idf.py -p /dev/cu.YOUR_STOPWATCH_PORT flash
```

The helper pins M5Stack M5GFX 0.2.19, M5IOE1 1.0.8 and M5PM1 1.0.6 by full commit.
PM/IO drivers share a native ESP-IDF I2C bus with touch using their documented
bus-handle overload; component CMake wrappers remove unused i2c_bus dependencies.
No optional driver/address path is selected at runtime. IO expander address 4F,
PMIC address 6E and touch address 15 have been confirmed on the user's unit.
Display wiring/panel init is adapted from the official StopWatch UserDemo.
The panel framebuffer is 468x466; the physical viewing area is round.

The demo is paired to the two physical BLE addresses already verified in transport.c. It is not a general device-picker or configuration product.
Start serial capture before connecting BLE; closing USB can reset StopWatch.

## Walkthrough

1. Disconnect the phone apps. Enable
   **Ratio + Auto**. Keep the platform empty when powering the watch.
2. The watch finds the scale, discovers service/characteristics, subscribes to
   **both** FF12 and FF11, then opens the brewer carousel.
3. Swipe or short Next to choose V60 / Switch / AeroPress / Hoop. Confirm by
   releasing the bottom brewer name, or short Select.
4. Confirming the brewer first initializes the scale, including any old result.
   Keep the platform empty during initialization. After Preparing scale, the
   empty-server page is armed. If needed, Reset with the empty
   platform. Place the empty server; >10g and 5 continuous stable seconds capture
   its measured mass. The rounded gray/green bar below the number follows the actual measurement window.
5. Weigh and save the dose on the scale as part of bean preparation, even before
   powering on the watch. The watch reads the stored dose with official command
   14 / response 10, preserves it across UI navigation in the same connection,
   and consumes later explicit 0F changes. No resaving is required here.
6. Place the full brewing assembly and release the single watch **Start** button
   (or Select). It writes Auto Ready 18/00 and shows Ready only after event 02.
   It does not start timing. Pouring causes the real Start event to enter brewing.
   Stop works from watch/Select or the scale;
   only the actual Stop notification freezes time, dose and added water.
7. After confirmed Stop, the watch shows `Remove server`. Lift the server off
   the scale; no Reset or other key is needed.
8. BOOKOO Auto Done reports negative gross server mass. Beverage is its absolute
   value minus the saved empty-server mass. Weight < -30.0g and positive beverage,
   stable within 0.2g for 1 second of notifications, enters `Waiting TDS`.
9. Only now the watch discovers/connects/subscribes RS FF01. Perform a **new**
   normal RS measurement. Exact level-1, CRC-valid single-result events produce
   the report; calibration/other routes, errors and earlier results do not.
10. Swipe numerical report <-> chart. Both use the same measured result and
    beverage-based extraction `TDS_percent * beverage_mass / dry_dose`.

No automatic tare is assumed at connection. Reset dispatch does not advance a
page or create a synthetic confirmation. Independent weight notifications drive
capture. Initial capture takes 5 seconds; server-removal capture takes 1 second.
These primary product rules are evaluated only when weight notifications arrive; a
>1 second gap restarts the window. Rendering never advances business state.

## Input

- Next short: carousel forward only. No bottom highlight or preparation Reset.
- Select short: visible enabled bottom action, including the single preparation Start.
- Press highlights; release executes. Leaving the original touch region cancels.
- Hold Next 650ms: Back. In a live brew this requests Stop and waits for its real
  event before returning to preparation with the scale-stored dose. Revisiting
  a stopped brewing page shows its frozen evidence; Stop returns to final weighing
  without sending a duplicate stop or fabricating another completion.
- Hold both keys 900ms: new Home/session. Chord consumes short actions.
- Reports ignore short physical presses; swipe or long Home/Back only.
- Scale loss interrupts the current brew and clears the stability window. Long
  Home begins a new session; reconnect never automatically resumes old brewing.

Animation, input debouncing and display scheduling use UI time. Business command
completion has no polling, timer expiry, fallback, guessed tare ACK, or alternate
RS source. Missing events remain pending until explicit cancellation/Home.

## Development console

`status`, `screen`, `home`, `next`, `select`, `back` are diagnostic commands.
They invoke the same UI actions; there is no command to inject a measurement.
`screen` emits SCREEN565/PX rows for inspecting the real framebuffer. Capture
with `tools/ble_capture.py --quiet-rx --quiet-pixels` to suppress raw screen rows
in the terminal while preserving them in the log. Keep a single serial session.

## Validation boundaries

The portable `coffee_flow` native tests cover complete measured data flow, exact
Start/Stop gating, Reset without a synthetic ACK, strict stability, missing dose,
stale TDS, error results, frozen Stop evidence, disconnect and Back navigation.
They complement the previously verified physical BLE protocols. The complete
human hardware walkthrough and physical visual/button/touch acceptance are
summarized by the acceptance checklist; implementation alone is not proof of
every physical acceptance check passing.


Auto Ready interfaces come from the official BOOKOO N 3.8.2 Android client.
`03 0A 18 00 00 11` -> Ready event 02; `03 0A 18 01 00 10` -> Exit Ready event 03.
Ready is a distinct read-only page; timing begins only on Start event 01.
The preparation page has no Reset or manual timer-start action. A pending Ready
write cannot complete via ATT success, normal weight, elapsed time or another event.

Stored dose acquisition is one post-subscription query `03 0A 14 00 00 1D`, with
CRC/XOR-validated response `03 10` and unsigned BE24 centigrams at offsets 6..8.
There is no periodic re-query or alternate-value path. Changes arrive as 03 0F.
The connection-handshake buffer protects a snapshot arriving before ATT completion.
Disconnect invalidates the cache; a new connection queries the actual scale again.

## Idle shutdown

On every screen, 10 minutes without touch/button interaction or a state change
powers off StopWatch through M5PM1. Page/session, scale phase, connection and
interruption transitions restart the interval. Weight notifications, timer display
updates, rendering and diagnostic status reads do not keep the device awake.

## Shared visuals and motion

The [visual system](../../spec/design.md) defines the current typography, color
palette, components, layout limits and animation timings. `main/design.h` owns
shared tokens. The renderer has display-only page entry, value-change tint and
Ready text-color breathing, alongside the brewer carousel.
Sensor values change immediately at their actual rounded value; animation never
interpolates measurements or completes a business operation. UI animation also
does not reset the global idle shutdown interval.

## Fonts and battery

The UI uses native-size grayscale-antialiased Noto Sans Medium fonts, not scaled
monochrome bitmaps. Generated VLW data is committed; rebuilding it requires
Pillow and `python3 tools/make_fonts.py` from this directory. Source font and OFL
license are in `assets/fonts`.

The top-center battery widget is present on every page. Its percentage follows
the official StopWatch UserDemo M5PM1 voltage estimate (3300..4200mV, filtered),
sampled once per second. It is an estimate, not a fuel-gauge measurement. A failed
read shows --%; battery updates do not reset inactivity or change brew state.

## Scale power-off after settlement

Once beverage capture has advanced to Waiting TDS, the scale is no longer
required. Its disconnection preserves the settled dose, water, time and beverage
and does not interrupt RS measurement or the final report/chart. Earlier scale
loss still interrupts an active brew. Returning from Waiting TDS to reweigh
requires the scale; if it is off, Back returns Home for an explicit new session.

## Brewer selection initializes the scale

Confirming a brewer displays Preparing scale and blocks empty-server capture.
A single mode query (0F -> 0E) must report Ratio (byte 2 = 2) and its Auto submode
(byte 5 = 2). Unsupported configuration is an explicit interruption. Then a fixed
single-pass sequence dispatches Exit Ready (18/01), Exit Done (07), and tare (01),
in this order. Each write is sent once, serialized by its ATT result; there is no
retry or alternate sequence. Exit Ready prevents 07 from starting a Ready scale;
in Auto Idle, 07 is a documented no-op. Done exit is event 04 when applicable.
ATT success is dispatch status, never a tare-completed assertion. Only subsequent
actual weight notifications can complete the independent 5-second empty-server
capture rule. Mode query or write failure cannot advance capture.

## Wi-Fi provisioning

The idle screen has a Wi-Fi setup bottom action. Enter setup, open the
[GitHub Pages setup page](https://e7h4n.github.io/coffee-watch/) in Chrome, pair
using the watch's six-digit code if asked, and enter 2.4 GHz network details.
Only a real IP event confirms success. Credentials are saved on the watch only
after connection; no password is returned or logged. Close/Back stops BLE setup
without shutting down Wi-Fi. Boot makes one attempt to join the saved network;
there are no automatic reconnect loops or alternative provisioning paths.

The custom 4 MiB factory partition keeps the original NVS/PHY offsets. Flash the
complete project, including its partition table, when upgrading from an older
firmware. See [the protocol and browser guide](../../spec/wifi.md).
