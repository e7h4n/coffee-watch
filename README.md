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
4. **Capture beverage mass.** After removing the dripper triggers Stop, follow the
   prompt to lift the server. Beverage mass is calculated automatically without
   another manual tare.
5. **Measure TDS.** Measure your coffee with the refractometer to receive a fresh
   concentration result.
6. **Review the report.** See coffee dose, added water, beverage mass, brew time,
   brew ratio, TDS, extraction yield, water retention per gram of coffee, and a
   Golden Cup chart.

Mass and volume use one decimal place. Extraction yield uses measured beverage
mass:

```text
Extraction yield (%) = TDS (%) × beverage mass (g) / dry coffee dose (g)
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

## Google Drive setup

The [device setup page](https://e7h4n.github.io/coffee-watch/) also accepts your
own Google OAuth client ID and client secret. They are sent to the watch over
authenticated Bluetooth, not uploaded to the website. The watch performs Google's
device authorization flow directly over HTTPS and keeps its refresh token locally.
Use a **TVs and Limited Input devices** client with Google Drive API enabled and
the `drive.file` scope. Follow the displayed Google authorization link/code.

A successful setup creates the watch's **Coffee Watch** folder and writes/reads
`connection-check.json` before confirming read/write access. This release sets up
Drive access; automatic brew uploads are not enabled. See [Drive setup](spec/drive.md)
for configuration, token lifetime, local removal and storage details.
