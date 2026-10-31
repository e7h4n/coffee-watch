# Coffee Watch — product specification

Status: product baseline agreed through interactive prototyping, 2026-10-31.

This document describes the final behavior after the design revisions. It supersedes older prototype alternatives and comments. All device UI and simulator copy is English.

## 1. Purpose and hardware

Turn M5Stack StopWatch into a compact brewing companion that:

1. Selects the brewer.
2. Remembers an empty server's mass automatically.
3. Follows the scale's dose, brewing timer and added water.
4. Measures final beverage mass automatically.
5. Receives a fresh TDS measurement from R2.
6. Shows a numerical report and a Golden Cup chart.

Hardware:

- M5Stack StopWatch: round 466 × 466 AMOLED touchscreen, upper-left physical Next key and upper-right blue Select key.
- BOOKOO Themis Ultra BLE scale.
- DiFluid R2 Extract BLE refractometer.

During brewing the **entire server + dripper + paper + coffee assembly sits on the scale**. Water gain can therefore represent added water. A separately supported dripper is outside this baseline.

Brewers: V60, Hario Switch, AeroPress, Ceado Hoop. Hoop is a radial-infusion brewer, not an ordinary immersion brewer.

## 2. Design principles

- Small-screen clarity: few large values, generous whitespace and restrained field labels.
- Do not add explanatory secondary labels or bottom stage descriptions by default.
- Exception: `R2 Disconnected` is a deliberately small, faint secondary status below `Waiting TDS`.
- A primary heading may identify a weighing state; overview and chart reports have no page title.
- Physical short-press actions must be inferable from the visible UI.
- Next never highlights a bottom action. Select mirrors an enabled bottom action.
- Most workflow advancement is automatic, driven by scale stability or measurement events.
- No dose selector, ratio selector, target-water selector, manual TDS mode or offline fallback workflow.
- Do not prompt users through filter placement, adding grounds or other routine assembly steps on the watch.
- R2 is not required to connect or remain connected during brewing.

## 3. Physical buttons and touch

| State | Next short press | Select short press | Touch |
| --- | --- | --- | --- |
| Connect scale | None | None | No brewing action |
| Select brewer | Cycle forward | Confirm visible brewer via bottom action | Swipe icons left/right; bottom action confirms |
| Empty-server weighing | None | Same Reset action as bottom button, unless locked | Bottom Reset only; content tap does nothing |
| Preparation split screen | Reset scale (left half) | Start timer (right half) | Press corresponding half |
| Brewing | None | Same Stop action as bottom button | Bottom Stop only |
| Final beverage weighing | None | Same Reset action as bottom button, unless locked | Bottom Reset only |
| Waiting TDS | None | None | Read-only |
| Final numerical/chart reports | None | None | Swipe left/right to switch reports |

Latest consistency rule overrides the temporary design where Select was inactive on final weighing: **a visible enabled bottom Reset maps to Select**.

### Press lifecycle

- Feedback occurs on press, execution on release.
- A bottom action is highlighted only while that bottom region or Select is actively held.
- Hovering the mouse without pressing does not highlight it.
- Swiping icons, tapping the content area and pressing Next must not highlight it.
- Release outside the original touch region cancels the action; crossing from one split half into the other does not execute either action.
- Pointer cancel, lost capture, focus loss and page/state changes clear stale feedback.
- Rerendering live data must not lose or leave behind the active press state.
- Disable actions while capturing stable weight or awaiting a command acknowledgement.
- Do not permit SVG/text drag selection within the device screen.

### Long presses

- Hold Next for approximately 0.65 seconds: Back. On either final report, return Home.
- Hold Next + Select together for approximately 0.9 seconds: return Home from any stage.
- A chord consumes both short presses. Releasing either key must not trigger Next, Select, tare or Start.
- Home is the scale-connection entry state; discard the current brew, timers, stability windows and pending command callbacks.
- Final reports cannot be dismissed by a short press or tap. Swipe only changes report; a long-press Home/Back is required to leave.
- Keyboard simulator: left arrow = Next, right arrow = Select; holding reproduces long presses; Escape simulates Back.

## 4. Workflow

### 4.1 Home: connect scale

- First screen: `Connect scale`.
- Connect only the scale, then enter brewer selection.
- Do not wait for R2 and do not show a two-device connection gate.
- Product expectation: the scale is zeroed when initially connected. The prototype simulates this. Firmware must validate the actual scale's initial reading; connection itself is not a documented tare guarantee.
- Scale loss pauses operation, retains progress and clears the stability window. Reconnection must not fabricate missing weight or timer data.

### 4.2 Select brewer

- No top heading.
- Current icon is large and centered on the **brewer body's centerline**, not on the bounds of handles or levers.
- Smaller previous/next icons appear at left/right.
- Forward navigation moves the right icon leftwards into the center while enlarging it; the former center moves left and shrinks. Reverse swipe mirrors the transition.
- Loop through all four brewers, including last-to-first. Queue rapid navigation without skipping inconsistent state.
- Reference motion: about 380 ms, ease-out. Respect reduced-motion preferences.
- Current scale ratios: center 1.18; side 0.34 relative to the base icon box.
- Bottom full-width circular-segment button displays the selected brewer's name. This is the sole touch confirmation target.
- Tapping the icon/content area does not confirm. Swiping there only changes selection.

#### Icon assets

- V60 and AeroPress follow the two supplied solid geometric reference images, reconstructed as SVG.
- V60 has **no handle**; retain fan-like ribs and horizontal base.
- Switch follows the same visual language: ribbed cone, thick base and a small inclined rounded-rectangle side lever. Keep a deliberate gap between lever and base; no protruding dot beneath the base.
- Center Switch on the cone/body axis, ignoring the lever.
- Hoop emphasizes outer water reservoir and central cylindrical tower.
- Prefer balanced silhouettes, consistent negative space and rounded geometric parts over detailed illustrations.

### 4.3 Empty-server weighing

- Automatically arm capture upon entering; **no mandatory Reset press**.
- Display live scale weight centrally, including zero and negative readings, with `g`.
- A bottom `Reset` action sends the scale's tare command; Select performs the same action.
- Once reset/zero is acknowledged within ±1g, keep the number visible at approximately zero; top copy is `Place empty server`.
- Do not replace the live number with a separate instruction-only page.
- When armed and weight is **strictly above 10g**, keep top copy `Empty server`, remove the Reset action and disable touch/Select/reset while capturing.
- Once weight stays stable for 5 seconds, save the actual empty-server mass and automatically enter preparation.
- Initial capture is already armed by default; manual Reset remains optional before the threshold is crossed.

### 4.4 Stability capture, shared rule

Applies to empty-server mass and final net beverage mass:

- Weight must be strictly >10g.
- Reference stability tolerance: maximum minus minimum within a continuous 5-second window ≤0.2g. This is a prototype tuning value, to be validated on hardware.
- A progress ring around the number makes exactly one revolution over those five seconds.
- The ring is driven by elapsed stable time, not an independent cosmetic timer.
- If the tolerance is exceeded, restart the five-second window and ring from zero using the new reading as the starting reference.
- At/below 10g, tare, disconnect or leaving the state clears the window.
- Changes in weight must still be displayed during the capture; do not freeze an early value.
- Save the current stable reading, then automatically advance.

### 4.5 Preparation: Reset | Start Timer

- No instructions for placing a dripper or adding coffee.
- Screen split into two equal vertical touch regions.
- Left region: live scale weight above `Reset`.
- Right region: two-line `Start Timer`, aligned with the left region's two lines.
- Next executes Reset, Select executes Start. Both halves follow press-highlight/release-execute semantics.
- Reset may be repeated; it visibly brings the live reading to zero and does not discard the scale-provided coffee dose.
- Read coffee dose from the scale's dose interface; do not infer dose from the full assembled load or ask the user to choose it.
- Start issues a scale timer command. Enter brewing after the scale confirms/reflects that it started. Starting directly on the scale must also be supported when its event is available.
- Do not guess or default to a 15g dose in real firmware. The simulator uses a 15g packet when `Add 15g coffee` is pressed.
- Missing valid dose must not yield a bogus ratio; production should remain pending until the real scale data is available.

### 4.6 Brewing

- Two principal lines: `Water` with added water in ml, then live brew ratio `1:x`.
- Dose is read from scale data and used in the live ratio.
- Stop is a full-width bottom circular-segment action, not a floating pill.
- Select mirrors Stop. Next short press does nothing.
- On release, send the scale Stop command; transition after receiving the scale stop event/acknowledged state, just as for a stop initiated at the scale.
- Freeze elapsed brew time, dose and added-water mass at the stop event. Later object movement/tare must not overwrite them.
- Do not auto-end at a pause between pours or during blooming.
- Water in ml is a UI convention approximating water mass at 1g/ml. The actual scale supplies grams; retain raw mass for calculations.

### 4.7 Final beverage weighing

- After Stop show `Place empty scale`, the current scale reading and bottom `Reset`.
- User removes the equipment, tares the empty scale and places the same server containing coffee back on it. Do not prompt through every physical action.
- Reset/Select use the same bottom-action interaction policy as initial weighing.
- After tare, use `Place server` while waiting, then `Beverage weight`.
- Net beverage mass = full-server mass minus saved empty-server mass.
- When net mass >10g and stable for five seconds, automatically freeze the measurement and enter the TDS-wait state.
- Show net mass once the server is back; apply the same ring, reset-lock and stability rules.
- Production must not silently accept tare with a full server still on the scale as a valid empty-scale baseline.

### 4.8 Waiting TDS

- Only now discover/connect the R2 if necessary.
- Retain known data in a 2×2 layout: Coffee dose, Brew ratio, Beverage, Brew time.
- Labels and values are centered within each cell, like the final report; use a generous content area and large values.
- Under the data: `Waiting TDS` **regardless of connection state**.
- Below that, show small faint `R2 Disconnected` only while disconnected.
- Always reserve the secondary line's height; hiding it must not shift any other content.
- No Connect R2 button on the watch. The prototype's external simulator has a connect control.
- A fresh valid final measurement automatically opens the reports; no Select confirmation.
- Temperature-related waiting/loop/long-running states are supported by the R2 protocol. Wait for its valid final result; never invent a countdown or use an intermediate/status packet as a TDS reading.
- Do not reuse an old measurement, calibration zero or previous brew's result.

### 4.9 Final numerical report

No `Overview` heading. Vertical reading order:

1. Brew time.
2. Coffee dose, added Water, Beverage mass on one row.
3. Brew ratio and retention per gram on one row.
4. TDS and Extraction on one row, largest/highest-emphasis values.

- Keep consistent column widths, baselines, number typography and whitespace. Avoid visually competing font scales and extra explanations.
- Preserve actual stopped water quantity; it was absent in early versions and is required.
- Swipe left/right to the chart report. Next and Select short presses do nothing because there is no bottom action.

### 4.10 Golden Cup report

- Large chart with internal tick labels to maximize usable plotting area.
- No `Golden Cup` title and no `TDS %` / `EY %` axis titles.
- Keep numeric axis ticks and a point for the current brew.
- Classic reference rectangle: extraction 18–22%, TDS 1.15–1.35%; **dashed outline only**, without fill or in-box title.
- Under the chart, show TDS and Extraction side by side using the same labels and numerical values as the numerical report.
- Chart is a reference, not an automatic taste-quality verdict.
- If a point lies beyond plot limits, keep truthful numerical results; the prototype clamps the point to the border. Production should consider expanding limits or marking out-of-range values explicitly.
- Swipe switches back. Leaving either report requires a long press.

## 5. Calculation definitions

Let D = dry dose in grams, W = added-water mass, B = net beverage mass, c = TDS as a fraction (1.40% → 0.014).

| Metric | Definition |
| --- | --- |
| Brew ratio | W / D, displayed as 1:x |
| Beverage mass | full-server mass − empty-server mass |
| Apparent retention per gram | (W − B) / D, displayed in g/g |
| Beverage-based extraction (%) | 100 × c × B / D |
| Full-immersion model estimate (%) | 100 × c × W / [(1−c) × D], only under its assumptions |

The displayed `Extraction` and chart currently use **beverage-based recovery**, consistently on both reports. They are not a claim to measure every dissolved solid remaining in the grounds.

Retention is an apparent loss metric including evaporation, paper retention and other losses; dissolved solids also affect mass balance. It is not a direct measurement of the grounds' pure absorption coefficient.

The product must distinguish percolation, immersion and hybrid techniques in any future model feature. A Switch can be used in more than one way; the tool name alone cannot determine an exact total-extraction formula. Hoop is radial infusion. Do not silently apply the full-immersion estimate to all brewers or present model estimates as measured results.

The user asked for paper-backed definitions. Relevant primary references:

- [Liang et al., 2021, Scientific Reports: full-immersion equilibrium model](https://pmc.ncbi.nlm.nih.gov/articles/PMC7994670/).
- [Guinard et al., 2023, Journal of Food Science: updated brewing control chart](https://doi.org/10.1111/1750-3841.16531).

Do not infer that a normal short brew has reached the equilibrium conditions of the immersion paper.

## 6. Bottom action styling

All single-action device screens use the same component:

- Full screen width, anchored to the bottom, about 24% of usable screen height.
- Straight top divider; bottom boundary follows the round screen's clipping.
- Centered accent text on a subtly different background.
- Not a pill, floating rectangle or detached button.
- Persistent pressed-state owner ensures live rerenders do not create stuck hover.
- Used for brewer-name confirmation, Reset and Stop.

## 7. Prototype simulator and scope

`prototype.html` is the latest dark/green prototype, independent of other files. It is the visual reference for this specification, subject to documented simulation limitations.

External simulator actions:

- Connect/disconnect scale and R2.
- Place/remove server, place/remove dripper, add 15g coffee, remove all.
- Reset/tare scale, start/stop timer, pour +30g.
- Set simulated TDS and send an R2 completed-result event.
- Read scale weight, dose, timer, ratio and stability progress.
- Restart session.

Physical mass changes are simulated; final beverage yield uses a 1.8g/g retention assumption solely to create test data. Never hardcode that assumption into production measurement or report calculation.

The prototype does not implement real BLE, persistent brewing history, device flashing, temperature-stability UI or reliable real-hardware dose acquisition. It does not grant permission to add new modes, settings or secondary copy beyond this specification.
