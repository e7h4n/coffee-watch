# Two-scale brewing

With the BOOKOO and a Lefu CK811 both connected, a brew needs no empty server, no
tare and no scale action: the watch finds which scale is which, starts at the pour
and ends when the dripper has drained.

## Setup

A stacked stand: the **total scale** carries the stand, the **cup scale**, the server,
the dripper and the water; the cup scale carries only the server. Which scale sits
where is not configured. The printed stand puts the CK811 (5 kg) under the stand and
the BOOKOO (2 kg) under the server; the reverse also works within the scales' ranges.
The stored dose always comes from the BOOKOO, so a CK811 alone shows `Connect BOOKOO`.

## Scales and preparation

- The watch keeps scanning for the BOOKOO (by address) and the CK811 (advertised as
  `LEFU-CK811` or `LFSmart Scale`) and reconnects either when it returns.
- Choosing a brewer with two scales connected skips empty-server capture: after the
  BOOKOO mode check the watch shows Ready once the dose is known (`2 scales` under
  the battery).
- The choice stays open until the pour: a second scale connecting during
  empty-server capture, while waiting for the dose or at a single-scale Ready switches
  to two-scale Ready, discarding the empty-server mass.
- Losing either scale before the pour returns to the brewer carousel (Home when no
  scale is left), not to an interruption. During a brew it interrupts; after the end
  it does not matter.

## Brewing

| Step | Rule |
| --- | --- |
| Start | Bed probe 3.0 °C above its 3 s median and, within 1.5 s, either scale 0.5 g above its own zero. Each scale's zero is its latest plateau (≥1 s within 0.2 g) in the 5 s before the trigger; brew time starts at the first 0.3 g rise |
| Roles | The scale that rose is the total scale. Its gain is never smaller than the cup scale's in a stacked stand, so a cup-scale gain 2 g larger for 2 s swaps the roles |
| Values | Water = total gain, beverage = cup gain, in the dripper = water − beverage. Out flow = beverage change over the last 2 s. For Switch and AeroPress the drain start (valve open, press) is the first sample 0.3 g up in the first 2 s window reaching 1 g/s |
| End | For 5 s: water within 0.5 g (not pouring), beverage within 0.3 g (not dripping), beverage ≥ 5 g and ≥ half the water (a steep or a bloom never ends it). Brew time ends at the beverage's last 0.1 g rise. A settled water level 30 g lower (dripper lifted) also ends it |
| After the end | Straight to Waiting TDS with the cup scale's beverage. Until the TDS arrives late drips raise the beverage, a 5 g drop (server lifted) freezes it, and 3 g more water for 0.5 s resumes the brew with the same clock |

A scale that only notifies when its reading changes is treated as steady between
notifications: its last value holds for the plateau, the end window and the out flow.

## Display

- Brewing: water as the hero, then Ratio, Bed and **Out** (g/s, in the beverage
  colour) at 34px; the chart adds the beverage band in front of the water steps and,
  for Switch/AeroPress, a dashed line at the drain start.
- Curve page: water (ml) labelled at its top, beverage (g) at the bottom right and
  `Open` at the drain start.
- Report: unchanged; retention is now measured, not derived from a reweighed server.

## Records

Brew records are `schema_version` 4 with `scales` (1 or 2), `drain_start_ms` and
`peak_out_flow_gps` (null with one scale). Two-scale trace CSVs add `out_g`; their
`weight_g` is the added water.

## Thresholds

All thresholds above come from simulated brews (`coffee_flow/test/test_duo.c`), not
yet from real two-scale traces; the CK811's notification rate is not known yet.
