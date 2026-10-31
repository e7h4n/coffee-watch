# Acceptance checklist

These checks preserve the product corrections represented by the prototype. Real BLE tests remain separate from browser simulation.

## Entry and navigation

- [ ] Home only requests the scale; R2 can remain disconnected throughout weighing and brewing.
- [ ] Connecting the scale enters the four-brewer carousel.
- [ ] Swipe in both directions and Next move through a cyclic sequence with proper movement and scaling.
- [ ] V60 has no handle; brewer-body centerlines align at screen center.
- [ ] Tapping the icon/content region does not confirm.
- [ ] Pressing bottom brewer name highlights it; release confirms; release outside cancels.
- [ ] Next never highlights the bottom button, including repeated presses and animation.
- [ ] No persistent button highlight or selected/dragged SVG remains after release, navigation or focus loss.

## Button policy

- [ ] Select mirrors any visible enabled bottom action, including both Reset pages and brewing Stop.
- [ ] Pages without a bottom action have no short Select action.
- [ ] Next only cycles brewers, except for left Reset on the preparation split page.
- [ ] Final reports respond to swipes, not short Next or Select.
- [ ] Hold Next returns Back; from reports it returns Home.
- [ ] Hold both keys returns Home and consumes short actions on release.
- [ ] Touch and physical-key feedback occur on press, action on release.

## Initial weighing

- [ ] After selecting a brewer, capture is already armed without a required manual Reset.
- [ ] Live numeric reading remains visible before and after optional tare, including near-zero values.
- [ ] Tare is available only through the bottom button or Select, not the main content area.
- [ ] Above 10g, Reset is hidden/disabled and Select cannot tare.
- [ ] At exactly 10g or less, no automatic capture occurs.
- [ ] A stable 5-second interval captures and advances exactly once.
- [ ] At 2.5 stable seconds, the circular indicator is approximately half complete.
- [ ] Drift outside 0.2g restarts the interval; loss of scale connection clears it.

## Preparation and brewing

- [ ] Preparation shows two equal halves: live weight + Reset on left; Start Timer on right.
- [ ] Physical Next and Select highlight only their own half and execute on release.
- [ ] Dragging between halves cancels the original touch action.
- [ ] Repeated tare visibly zeros the scale and does not discard the scale-reported dose.
- [ ] No dose or ratio selection page exists.
- [ ] Starting follows the scale event; the prototype simulates a short command/event delay.
- [ ] Brewing displays Water and live 1:x ratio.
- [ ] Stop is a bottom circular segment, not a pill.
- [ ] Touching brewing content does not stop the timer.
- [ ] Stop release dispatches the command and the stop event advances the workflow.
- [ ] Dose, water and time remain frozen after stop, object removal and subsequent tare.

## Beverage and R2

- [ ] Final weighing displays live raw reading before tare and net beverage reading after replacing the server.
- [ ] Net beverage >10g and stable for 5s automatically advances to Waiting TDS.
- [ ] Same ring/reset lock behavior applies as initial weighing.
- [ ] Known dose, ratio, beverage and brew time remain visible while waiting for TDS.
- [ ] Labels and values are centered; waiting layout uses the available content area.
- [ ] `Waiting TDS` is always the primary wait copy.
- [ ] `R2 Disconnected` is small/faint and disappears without shifting layout upon connection.
- [ ] R2 can connect only at measurement time in the simulator.
- [ ] A final valid new TDS result automatically enters the report.
- [ ] Intermediate/high-temperature/calibration/error/stale results do not masquerade as valid coffee concentration in firmware.

## Reports

- [ ] Numerical report has no Overview title.
- [ ] Display order: time; coffee/water/beverage; ratio/retention; TDS/extraction.
- [ ] TDS and extraction share one prominent row.
- [ ] Water is the stopped brew's added water, not the later scale load.
- [ ] Both reports use the same TDS/extraction values and calculation basis.
- [ ] Golden Cup chart has no page title and no TDS%/EY% axis titles.
- [ ] Numeric ticks sit inside the plot; the reference zone is an unfilled dashed rectangle.
- [ ] Swipe switches reports; taps/short physical presses cannot exit or restart.
- [ ] Only long-press navigation returns Home.

## Simulator and portability

- [ ] `prototype.html` opens without external scripts, fonts, assets or build dependencies.
- [ ] UI and simulator copy is English.
- [ ] External action controls simulate placing/removing items, dose, tare, timer, pours and TDS.
- [ ] The simulator panel is clearly distinct from device UI.
- [ ] Normal and narrow browser layouts keep critical values inside the circular screen.
- [ ] Pending simulated command callbacks cannot advance a reset/new session.
