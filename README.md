# Coffee Watch

**A coffee assistant designed for specialty pour-over brewing.**

Coffee Watch connects an M5Stack StopWatch, a Bluetooth coffee scale, and a
refractometer. A defined brewing workflow brings measurements from these devices
together to calculate and present your brew data with minimal manual recording.

## From brewing to a report

1. **Choose a brewer.** Select V60, Hario Switch, AeroPress, or Ceado Hoop.
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

- [Product behavior and interactions](spec/product.md)
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
