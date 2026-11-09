# Coffee Watch

**A coffee assistant designed for specialty pour-over brewing.**

Coffee Watch connects an M5Stack StopWatch, a Bluetooth coffee scale, and a
refractometer. A defined brewing workflow brings measurements from these devices
together to calculate and present your brew data with minimal manual recording.

## From brewing to a report

1. **Choose a brewer.** Select V60, Hario Switch, AeroPress, or Ceado Hoop.
   Selection initializes the scale and clears any previous brew result. Keep
   the platform empty until initialization finishes.
2. **Weigh the empty server.** Capture its mass automatically. Coffee dose comes
   from the value you saved on the scale during bean preparation.
3. **Start brewing.** Tap Start to arm Auto READY. Pouring starts the timer through
   the scale's own event, while the watch displays added water and brew ratio.
   From Start until the brew stops, the watch records weight and bed-probe
   temperature 10 times per second.
4. **Capture beverage mass.** After removing the dripper triggers Stop, follow the
   prompt to lift the server. Beverage mass is calculated automatically without
   another manual tare.
5. **Measure TDS.** Measure your coffee with the refractometer to receive a fresh
   concentration result.
6. **Review the report.** See coffee dose, added water, beverage mass, brew time,
   brew ratio, TDS, extraction yield, water retention per gram of coffee, a
   Golden Cup chart and the brew's temperature and water curve. Pages advance
   every 5 seconds; swipe or press Next to turn them yourself. Lifting the
   server also returns the scale to its idle, tared state for the next brew,
   and the watch starts the next brew on the brewer you used last.

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
| BOOKOO Themis Ultra | Coffee dose, weight, and brewing timer | Firmware 4.0.1, Ratio + Auto mode |
| DiFluid RS | TDS measurement | Firmware v002-005 |
| Bed probe | Brew temperature trace | 100 kΩ B3950 NTC on G8, G9 = 3V3/2 reference |

This is a prototype with real brewing walkthroughs completed. The firmware
connects to the two peers configured in its transport; a general device picker
is not yet available. The workflow depends on the events provided by these
devices and firmware versions. Other models and versions have not been validated.

## Wi-Fi setup

On the idle screen, tap **Wi-Fi setup**, then open the
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
device firmware uses ESP-IDF mbedTLS. Build outputs, downloaded drivers, and local
captures are excluded from version control.

## Dropbox setup

The [device setup page](https://e7h4n.github.io/coffee-watch/) links your own
Dropbox app. In the [Dropbox App Console](https://www.dropbox.com/developers/apps),
create an app with **Scoped access** and **App folder** access, enable
`files.content.write` and `files.content.read` under Permissions and press
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
