# Coffee Watch

**A coffee assistant designed for specialty pour-over brewing.**

Coffee Watch connects an M5Stack StopWatch, a Bluetooth coffee scale, and a
refractometer. A defined brewing workflow brings measurements from these devices
together to calculate and present your brew data with minimal manual recording.

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
this interval.

## Supported hardware

| Device | Role | Validated version |
| --- | --- | --- |
| M5Stack StopWatch | Interaction, device connections, and reports | ESP32-S3 |
| BOOKOO Themis Ultra | Coffee dose and weight | Firmware 4.0.1, Ratio mode (Auto not used) |
| DiFluid RS | TDS measurement | Firmware v002-005 |
| Lefu CK811 | Second scale for two-scale brewing (total or cup) | 5 kg / 0.1 g; protocol from the open-source Pourista driver, not yet validated here |
| Bed probe | Brew start and temperature trace | 100 kΩ B3950 NTC on G8, G9 = 3V3/2 reference |
| SparkFun SEN-16474 (MAX30101) | Roast color mode, on the RoastWatch adapter | G5 = SDA, G6 = SCL; not yet validated on hardware |

This is a prototype with real brewing walkthroughs completed. The firmware
connects to the two peers configured in its transport; a general device picker
is not yet available. The workflow depends on the events provided by these
devices and firmware versions. Other models and versions have not been validated.

## Roast color mode

Attach the RoastWatch adapter with its MAX30101 sensor and Home opens roast color
mode. Place it on a
scraped-flat sample cup: each placement records one reading after 2 stable
seconds, and three placements give the mean and its spread. Calibrate once on
fresh baking soda, which sets the IR LED so it reads 230. Every reading is
uploaded to Dropbox over the saved Wi-Fi as that group's JSON record. The index uses the
open-source Roast Meter's sensor settings and formula; it ranks your own roasts
and is not an Agtron value. See [Roast color mode](spec/colorimeter.md).

## Wi-Fi setup

On Home, tap **Wi-Fi setup**, then open the
[Chrome setup page](https://e7h4n.github.io/coffee-watch/). Connect over Web
Bluetooth, enter the pairing code shown on the watch if requested, and submit
your 2.4 GHz network details. Credentials go directly to the watch; the static
page does not upload or save them. See the [Wi-Fi interface guide](spec/wifi.md).

## Build and use

With ESP-IDF 5.5.x activated, run from `firmware/demo`:

```sh
python3 fetch_drivers.py
IDF_COMPONENT_MANAGER=0 idf.py build
idf.py -p /dev/cu.YOUR_STOPWATCH_PORT flash
```

The driver helper downloads M5Stack drivers pinned to specific commits. See the
[firmware guide](firmware/demo/README.md) for device preparation, controls,
building, and flashing.

## Development documentation

- [Visual system and shared components](spec/design.md)
- [Product behavior and interactions](spec/product.md)
- [Roast color mode](spec/colorimeter.md)
- [Two-scale brewing](spec/two-scales.md)
- [Wi-Fi setup and provisioning](spec/wifi.md)
- [Bluetooth interfaces](spec/bluetooth.md)
- [Acceptance checklist](spec/acceptance.md)
- [Brewer artwork](spec/icons/)
- [Browser visual prototype](spec/prototype.html): an early design reference with
  simulated hardware. The current firmware defines the implemented workflow.

Run protocol and state-machine tests from the repository root:

```sh
python3 -m unittest discover -s tools -v
```

Tests require a C compiler. The native AES harness uses macOS CommonCrypto;
device firmware uses ESP-IDF mbedTLS. The Dropbox fault-injection test also uses
cJSON and portable mbedTLS sources from an ESP-IDF checkout: set `IDF_PATH` to run
it (it reports an explicit skip if unavailable). Release verification should run
that test without skipping. Build outputs, downloaded drivers, and local
captures are excluded from version control.

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
alone does not override a previously generated `sdkconfig`.

Serial diagnostics: `dropbox status` reads the saved connection state;
`dropbox check` refreshes authorization and uploads the existing
`connection-check.json` test file, validating its returned content hash. Neither
command erases credentials. Logs include HTTP status, request/response sizes and
available TLS memory, never token response bodies or authorization headers.
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
