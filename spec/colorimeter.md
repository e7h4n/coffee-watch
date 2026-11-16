# Roast color mode

With the RoastWatch adapter and its roast-color sensor on the StopWatch, the watch
is a roast colorimeter; with a scale it is the brew assistant. Home chooses.

## Hardware

| Part | Connection |
| --- | --- |
| SparkFun SEN-16474 (MAX30101) | RoastWatch adapter J3: back-header G5 = SDA, G6 = SCL, 3V3, GND |
| Measuring glass | 25 × 12.5 × 1.0 mm glass in the housing's 26.4 × 13.4 × 1.0 mm pocket, flush with the bottom |
| Sample cup | 40 mm well, 4 mm deep, filled with ground coffee and scraped flat |

The sensor looks down through an 11 mm aperture and the glass, 3.4 mm above the
sample. The housing and cup must be printed opaque; pressure on the housing changes
the reading.

## Home selection

Home is the page with **Connect scale or color sensor**, the Brew and Roast color
tiles and Wi-Fi setup. Boot starts there, and holding both keys for 900 ms returns
there from any page. On Home the watch keeps searching for the scale and polls G5/G6
twice a second (I2C controller 1, 400 kHz, internal pull-ups in addition to the
sensor board's) for a MAX30101 with part ID 0x15:

- A **new** connection on Home enters its product, the first one winning: the color
  sensor appearing (also at power-on) enters roast color, the scale link coming up
  enters the brew assistant.
- A device that is still connected when Home is entered (for example after holding
  both keys) does not re-enter by itself; its tile is lit and a tap opens it.
- Wi-Fi setup suspends automatic entry; the tiles still show what is connected.
- Removing the sensor while measuring (a second of failed reads, or 3 s without a
  sample) returns Home; attaching it again re-enters roast color.

Wi-Fi, Dropbox and Bluetooth start at boot and serve both products. The scale may
stay connected in the background while measuring color; it is offered on Home.
Leaving roast color shuts the sensor down (LED off); entering it restarts the 60 s
warm-up.

## Sensor configuration

The same settings as the open-source Roast Meter (juztins-lab/roast-meter), so its
community calibration applies:

- SpO2 mode, red and green LEDs at 0 mA, IR (LED2) amplitude from calibration
  (0.2 mA per step; 135 before the first calibration).
- 50 samples/s averaged by 4 (12.5 readings/s), 411 µs pulses (18 bit), 16384 nA
  range, FIFO rollover. The watch drains the FIFO every 40 ms.
- Index = x − (117 − x) × 0.165, with x = IR / 1000. The Roast Meter community fitted
  it against a Lighttells CM-100. The index ranks this device's samples; it is not
  an Agtron reading and is not comparable with other instruments.

## Measuring

1. **Warm-up.** The first 60 s after entering roast color count down, as on the Roast Meter.
   Nothing is recorded and calibration is unavailable.
2. **Loaded.** IR at or above 30000 means something is on the glass (the Roast
   Meter's level); below 28000 it is lifted. The gap keeps the state from
   flickering.
3. **Reading.** A reading is the mean of 2 s of samples whose spread stays within
   0.3 % of their mean. A larger change or a gap over 0.5 s restarts the window; the
   stability bar shows the real window.
4. **Lift between readings.** After a reading the housing must be lifted and
   placed again, ideally after re-filling and scraping the cup. Staying on the
   sample never adds a reading.
5. **Result.** Three readings make a result: the mean with one decimal, the three
   readings and their sample standard deviation. An SD above 1.5 asks for a retest.
   Placing the housing again starts the next group.
6. **Saturation.** A reading at or above 260000 (the 18-bit ceiling) shows Too
   bright and records nothing; recalibrate on baking soda.

Back (Next held 650 ms) clears the current group; holding both keys 900 ms goes Home.

## Calibration

Calibrate is the bottom action once warm-up is over. Fill the cup with fresh
baking soda, scrape it flat and place the watch:

1. The placement must hold a stable 2 s window (a saturated reading counts as
   stable).
2. The watch measures at amplitude 255. Below the target (index 230, IR ≈ 213996)
   it fails as Too dark for 230, for example on coffee.
3. It bisects amplitudes 1–255 for the smallest one reading at least the target,
   measures the amplitude just below it and keeps the nearer of the two.
4. It measures the choice once more; the two measurements at that amplitude must
   agree within 2 %. That last reading is the stored baking-soda index, as close to
   230 as one amplitude step allows (about 0.7 % of the reading near amplitude
   150, coarser at low amplitudes).

Each step ignores 300 ms after the amplitude change and averages the next 800 ms.
A spread over 2 % within a step, or a repeat that disagrees (the housing moved),
fails as Sensor moved. There
are at most 11 steps, about 15 s with placement. Cancel, Back or a failure
restores the previous amplitude; success stores the amplitude and the measured
baking-soda index in NVS (`cw_color`: `ir_pa`, `soda_x10`), shown on later boots.

## Dropbox records

Every reading uploads its group as `/roast-color-<session>-<sequence>.json` in the
app folder; `session` is the watch's MAC plus a random boot value, as for brew
records. Each upload is the whole document, so the file always holds the readings
so far:

```json
{"schema_version":1,"kind":"roast_color","id":"…","revision":2,
 "created_at":"2026-11-15T12:00:00Z","status":"partial","group_size":3,
 "readings":[{"measured_at":"2026-11-15T12:00:00Z","index":78.4,"ir_raw":113456},
             {"measured_at":"2026-11-15T12:00:09Z","index":79.1,"ir_raw":114058}],
 "mean_index":78.8,"sd":0.49,"retest":false,"ir_led_amplitude":150,
 "baking_soda_index":229.5,"sensor":"MAX30101",
 "index_formula":"x - (117 - x) * 0.165, x = IR / 1000"}
```

- `revision` is the number of readings; `status` turns `complete` at the third.
  `sd` and `retest` are null with one reading; `baking_soda_index` is null before
  the first calibration. `ir_raw` is the window's mean IR.
- Times are UTC from SNTP; an upload waits up to 15 s for the clock and fails
  without it. A group that is cleared early stays `partial`.
- The first upload overwrites the path (unique to this boot); later ones update
  exactly the revision this watch wrote, so an edit made elsewhere is reported
  rather than replaced. A failed upload is not retried; the next reading uploads
  the whole group again.
- The line under the battery shows `Dropbox: saving`, `saved`, `save failed` or
  `Dropbox not set up` for the current group. The serial log has the path and
  error (`record color id=… state=… path=… error=…`).

## Serial console

| Command | Output |
| --- | --- |
| `status` / `color` | `COLOR page=… raw=… index=… loaded=… count=… mean=… sd=… led=… calibrated=… soda=…` |
| `stream` | Toggles one `IR t_ms=… raw=… led=…` line per sample, for setting thresholds |
| `calibrate` / `select` | Bottom action |
| `back` | Back |
| `home`, `mode`, `open brew`, `open color` | Home, the current product, a Home tile |
| `screen` | Framebuffer dump, as in the brew assistant |

Every reading logs `READING n/3 index=… raw=… led=…` and every group logs
`RESULT mean=… sd=… readings=… led=… soda=…`.

## Not yet validated on hardware

The thresholds come from the Roast Meter's geometry. On the first build, check
with `stream`: the lifted reading with the glass installed must stay below
28000, the darkest roast you measure must read above 30000, and fresh baking soda
must reach the target below amplitude 255.
