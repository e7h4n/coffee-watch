# Coffee Watch

**A coffee assistant designed for specialty pour-over brewing.**

Coffee Watch connects an M5Stack StopWatch, a Bluetooth coffee scale, and a
refractometer. A defined brewing workflow brings measurements from these devices
together to calculate and present your brew data with minimal manual recording.

## ESP Hub StopWatch

For a registered StopWatch running the ESP Hub launcher and fresh 4 MiB app layout, use the `esphub.json` asset in the Coffee Watch release. Check its board, version and SHA-256. With EspHub 1.1.144, enter the bootloader and pass the managed image URL from `esphub.json` to `ota.install` with `kind=native`. The bootloader writes the 4 MiB `app` partition and starts the application. Verify that the next app boot reports `coffee_watch_demo`, the release version and a valid application after its local display/NVS health check. [Coffee Watch releases](https://github.com/e7h4n/coffee-watch/releases)

ESP Hub manages Wi-Fi on this build. Hold Select for three seconds during application startup to return to the launcher. For Dropbox authorization, open **Dropbox setup** on the watch and pair with the [Bluetooth setup page](https://e7h4n.github.io/coffee-watch/). Battery percentage uses the M5PM1 voltage and the same conversion as M5Unified’s StopWatch power API. [Official battery example](https://docs.m5stack.com/en/arduino/stopwatch/battery)

## Home

The watch starts on Home: **Connect scale or color sensor**, a Brew and a Roast
color tile, and Wi-Fi setup. Whichever connects first picks the product: the scale
opens the brew assistant, the RoastWatch color sensor opens roast color mode. Hold
both keys for 0.9 s on any page to return Home; a device that is still connected is
then offered as a lit tile instead of reopening by itself. Wi-Fi and Dropbox are
shared by both.

## From brewing to a report

1. **Choose a brewer.** Select V60, Hario Switch, AeroPress, or Ceado Hoop.
   Selection initializes the scale and clears any previous brew result. Keep
   the platform empty until initialization finishes.
2. **Weigh the empty server.** Capture its mass automatically. Coffee dose comes
   from the value you saved on the scale during bean preparation.
3. **Start brewing.** Just pour; there is no Start button. Once the empty server
   and dose are known the watch is Ready, and it starts the brew itself when hot
   water reaches the bed probe, without the scale's Auto mode. Water poured
   before the probe reacts still counts: the zero is the last stable weight in the
   5 seconds before the pour. The watch displays added water and brew ratio, and
   records weight and bed-probe temperature 10 times per second.
4. **Capture beverage mass.** Lift the dripper off the server; the watch detects
   the lift and stops the brew. Leave the server on the scale and the beverage
   mass is calculated automatically without another tare.
5. **Measure TDS.** Measure your coffee with the refractometer to receive a fresh
   concentration result.
6. **Review the report.** See coffee dose, added water, beverage mass, brew time,
   brew ratio, TDS, extraction yield, water retention per gram of coffee, a
   Golden Cup chart and the brew's temperature and water curve. Pages advance
   every 5 seconds; swipe or press Next to turn them yourself. The watch starts
   the next brew on the brewer you used last.

With the BOOKOO and a Lefu CK811 both connected, steps 2 and 4 disappear: no empty
server and no lifting, the watch tells the two scales apart, measures the beverage and
the drip flow directly and ends the brew only when the dripper is removed.
Slow observed drip-weight steps can make a quiet advisory chirp; they never end the brew. See
[Two-scale brewing](spec/two-scales.md).

Mass and volume use one decimal place. V60 and Hoop use the measured beverage
mass:

```text
Extraction yield (%) = TDS (%) × beverage mass (g) / dry coffee dose (g)
```

Hario Switch is brewed with the valve closed for the steep, then opened until it
drains completely. AeroPress is steeped inverted or with the outlet closed, then
pressed out in one go, without bypass dilution. In both cases the spent grounds
hold brew at cup strength, so extraction counts all added water (full-immersion
mass balance, Liang, Chan & Ristenpart, *Sci. Rep.* 11:6904, 2021):

```text
Extraction yield (%) = TDS (%) / (100 − TDS (%)) × added water (g) / dry coffee dose (g) × 100
```

On any screen, the StopWatch powers off after 10 minutes without interaction or
state changes. Continuous weight updates and screen refreshes do not restart
this interval. Shutdown is suspended while an update check/download is busy.

## Supported hardware

| Device | Role | Validated version |
| --- | --- | --- |
| M5Stack StopWatch | Interaction, device connections, and reports | ESP32-S3 |
| BOOKOO Themis Ultra | Coffee dose and weight | Firmware 4.0.1, Ratio mode; Ratio + Auto for single-scale/no-probe fallback |
| DiFluid RS | TDS measurement | Firmware v002-005 |
| Lefu CK811 | Second scale for two-scale brewing (total or cup) | 5 kg / 0.1 g; protocol from the open-source Pourista driver, not yet validated here |
| Bed probe | Brew start and temperature trace | 100 kΩ B3950 NTC on G8, G9 = 3V3/2 reference |
| SparkFun SEN-16474 (MAX30101) | Roast color mode, on the RoastWatch adapter | G5 = SDA, G6 = SCL; not yet validated on hardware |

This is a prototype with real brewing walkthroughs completed. The firmware
connects to the two peers configured in its transport; a general device picker
is not yet available. The workflow depends on the events provided by these
devices and firmware versions. Other models and versions have not been validated.

### Single scale without a readable bed probe

First capture the empty server and save the dose as above. Put BOOKOO in
**Ratio + Auto**, assemble the brewer, then press **Start on BOOKOO** to enter
its Ready state. The scale detects water and starts the timer; lifting the dripper
ends it. The watch follows BOOKOO's Start/Stop, elapsed time and settled water.
Auto Done re-zeroes the scale: **lift the server off the scale** to capture beverage
mass (negative gross minus the saved empty-server mass), then measure TDS.

This fallback is selected only by the physical Auto Ready event, with one scale,
valid empty-server/dose references, and no valid probe reading for at least two
seconds. Invalid ADC readings can also mean a cold/saturated probe, not confirmed
physical disconnection. Probe arrival during this brew never switches ownership.
Leaving Auto Ready, discarding a reviewed brew, or leaving Auto Done before
weighing the server requires a new brew initialization; stale tare references are
not reused. Two-scale and probe-started brews retain their existing behavior.
Native tests cover this fallback; new-firmware hardware acceptance is still pending.

## Roast color mode

Attach the RoastWatch adapter with its MAX30101 sensor and Home opens roast color
mode. Place it on a
scraped-flat sample cup: each placement records one reading after 2 stable
seconds, and three placements give the mean and its spread. Calibrate once on
fresh baking soda, which sets the IR LED so it reads 230. Every reading is
uploaded to Dropbox over the saved Wi-Fi as that group's JSON record. The index uses the
open-source Roast Meter's sensor settings and formula; it ranks your own roasts
and is not an Agtron value. See [Roast color mode](spec/colorimeter.md).

## Standalone Wi-Fi setup

On Home, tap **Wi-Fi setup**, then open the
[Chrome setup page](https://e7h4n.github.io/coffee-watch/). Connect over Web
Bluetooth, enter the pairing code shown on the watch if requested, and submit
your 2.4 GHz network details. Credentials go directly to the watch; the static
page does not upload or save them. See the [Wi-Fi interface guide](spec/wifi.md).

## Standalone firmware updates

The 16 MiB watch now targets two 4 MiB application slots, with **7.93 MiB reserved
for future local data**. The standalone build has no on-watch update controls.

New OTA boots are confirmed only after a rendered UI, successful NVS startup and
ten seconds of watchdog-monitored main-loop health; otherwise the bootloader can
roll back on reset. Wi-Fi, scales and optional color hardware are not health gates.
Updates use verified HTTPS plus strict image identity, size and SHA-256 checks,
**not a publisher signature**. No Secure Boot or eFuse changes are made.

**The first upgrade from the existing factory layout needs a separately approved
USB migration**, with private full-flash/NVS/PHY backups and readback checks.
It is not an atomic OTA and cannot automatically roll back the old layout. Finish
and save every brew first: USB resets lose any volatile trace. Do not run a generic
`idf.py flash` or rewrite a fixed application address on an existing watch. Read
[the update and recovery procedure](spec/firmware-update.md). The reserved data
area is not yet a durable curve store or upload queue. Hardware migration, OTA and
power-loss acceptance remain separate from native tests and successful builds.

## Build and use

With **ESP-IDF 5.5.2** activated, run from `firmware/demo`:

```sh
python3 fetch_drivers.py
idf.py -DPROJECT_VER=dev build
```

Use the guarded USB procedure above for first migration. The driver helper downloads M5Stack drivers pinned
to specific commits. The firmware CI tests and builds PR candidates, and publishes
versioned application/manifest/checksums plus a separate USB-only ZIP on `main`.
An existing generated `sdkconfig` overrides defaults: use a fresh configuration
or deliberately migrate every required setting before packaging. See the
[firmware guide](firmware/demo/README.md) for controls and device preparation.

## Development documentation

- [Visual system and shared components](spec/design.md)
- [Product behavior and interactions](spec/product.md)
- [Roast color mode](spec/colorimeter.md)
- [Two-scale brewing](spec/two-scales.md)
- [Wi-Fi setup and provisioning](spec/wifi.md)
- [Firmware OTA, initial USB migration and recovery](spec/firmware-update.md)
- [Bluetooth interfaces](spec/bluetooth.md)
- [Acceptance checklist](spec/acceptance.md)
- [Brewer artwork](spec/icons/)
- [Browser visual prototype](spec/prototype.html): an early design reference with
  simulated hardware. The current firmware defines the implemented workflow.

Run protocol and state-machine tests from the repository root:

```sh
python3 -m unittest discover -s tools -v
```

Tests require C/C++ compilers, `esptool==4.11.0` for the migration no-retry
regression, and an ESP-IDF source checkout selected by `IDF_PATH` with cJSON and
portable mbedTLS sources. AES uses CommonCrypto on macOS and real mbedTLS on Linux.
Missing optional crypto/Dropbox sources explicitly skip coverage; release CI
supplies them and requires zero skips. Build outputs, downloaded drivers, private
migration backups and local captures are excluded from version control.

## Dropbox setup

The [device setup page](https://e7h4n.github.io/coffee-watch/) links your own
Dropbox app. In the [Dropbox App Console](https://www.dropbox.com/developers/apps),
create an app with **Scoped access** and **App folder** access, enable
`files.content.write`, `files.content.read` and `files.metadata.read` under Permissions and press
Submit. Enter its App key and App secret on the page, open the authorization
link, allow access on Dropbox and paste the code it shows. The key, secret and
code go to the watch over authenticated Bluetooth, not to the website. The watch
exchanges the code with Dropbox for a refresh token, so uploads keep working
(App Console generated access tokens expire after four hours and are not used).

A successful setup uploads `connection-check.json` to the app folder and checks
Dropbox's content hash. The watch uploads a brew JSON record when beverage weight
is confirmed, together with the brew's 10 Hz weight/temperature trace as a CSV,
then updates the same record when a valid TDS measurement arrives. See
[Brew records](spec/brew-record.md) and [Dropbox setup](spec/dropbox.md) for
configuration, storage and verification details.

TLS uses PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`) so BLE, the display and
speaker DMA do not leave it without a large enough internal RAM block. For an
existing build directory, select **External SPIRAM** under **Component config →
mbedTLS → Memory allocation strategy** in `idf.py menuconfig`; changing defaults
alone does not override a previously generated `sdkconfig`. Also apply these
settings to existing generated configurations (all are in the defaults):

- `CONFIG_MBEDTLS_HARDWARE_AES` disabled: its internal DMA bounce buffers can
  fail even with TLS allocated in PSRAM; software AES avoids that dependency.
- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` to offload network allocations.
- Static Wi-Fi TX buffers enabled, **4** buffers
  (`CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=4`). Enabling network PSRAM allocation
  selects a static pool; its default 16 buffers consumed nearly all remaining
  internal RAM on this watch. Do not enable that option alone.

Serial diagnostics: `dropbox status` reads the saved connection state;
`dropbox check` refreshes authorization and uploads the existing
`connection-check.json` test file, validating its returned content hash. Neither
command erases credentials. Logs include HTTP status, request/response sizes and
available TLS memory, never token response bodies or authorization headers.
HTTPS bodies are sent in at most 1 KiB writes, independently of the software-AES
configuration. A single large write with hardware AES previously failed despite
a successful small connection check. Logs also distinguish received HTTP status,
header-fetch failure and incomplete bodies rather than conflating all failures.
A passed connection check does not replace full brew/trace/TDS upload testing.
`dropbox test` exercises a synthetic 2,000-sample dual-scale CSV, draft JSON and
final JSON through the real worker, using labelled `/connection-check-*` files
and restoring the live brew's bookkeeping afterwards.

Failed drafts retain their trace in RAM so the same brew's later complete result
can recover after a transient failure. Metadata hash/revision checks reconcile
partial uploads without blindly overwriting files. `record status` prints the
current error; the result screen also shows it rather than only `save failed`.

Before uploading, summaries of the newest eight brews are backed up to NVS;
`record backups` exports them after a reboot. **Summaries do not preserve raw
curves, do not imply Dropbox success, and are not automatically re-uploaded.**
The full trace recovery slot is still volatile. See [Brew records](spec/brew-record.md).
