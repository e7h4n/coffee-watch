# Product behavior

## Hardware and flow

M5Stack StopWatch (468×466 framebuffer), BOOKOO Themis Ultra 4.0.1 in Ratio +
Auto mode, and DiFluid RS v002-005. Brewers: V60, Switch, AeroPress and Hoop.
The complete brewing assembly sits on the scale while pouring.

1. Connect the scale and select a brewer. RS is not needed yet.
2. Place an empty server. Capture weight >10g stable within 0.2g for 5 seconds.
   Optional Reset dispatches tare; ATT success never claims tare completion.
3. Prepare the brewing assembly. Dose is saved on the scale during bean weighing,
   possibly before watch startup. Read its stored dose at connection; no dose
   selection or resaving during preparation.
4. The single Start button requests Auto Ready. Show Ready only after its exact
   device event. Pouring produces the real Start event and enters brewing.
5. Show live added water, ratio and elapsed time. Stop from the watch or scale
   freezes dose, water and time from device events. Dripper removal can Stop Auto.
6. Show Remove server. BOOKOO Auto Done reports zero while the full server stays
   on the scale; removing it produces negative gross server mass. Beverage mass
   equals -live weight - saved empty-server mass. Require weight < -30.0g,
   positive beverage and <=0.2g variation for 1 second. No final Reset.
7. Connect RS and accept a new normal measurement. Show numerical report and
   Golden Cup chart, using the same result.

## Precision and calculation

Mass and volume displays and calculation inputs use one decimal place, rounded
half away from zero. The wire decoder retains centigrams. TDS retains sensor
precision and is displayed to two decimals. Water ml uses the scale's gram value.

- Brew ratio = added water / dry dose.
- Extraction percent = TDS percent × beverage mass / dry dose.
- Retention g/g = (added water - beverage mass) / dry dose.

No assumed retention, default dose, manual TDS or offline measurement path.

## Events and continuity

Business transitions follow validated notifications from the active connection.
Weight stability is evaluated only on weight notifications; gaps >1 second reset
continuity. UI rendering and elapsed timers cannot complete an operation. There
are no command retries, polling completion checks or fallback measurements.
Disconnect interrupts active brewing. Explicit Home begins a new session;
reconnection alone never resumes it. New connections invalidate the dose cache.
Home/navigation within the same connection preserve scale-reported stored dose.
Start freezes its own dose snapshot; later dose changes cannot alter a result.
Only a fresh, valid RS result after entering Waiting TDS creates a report.

## Input and appearance

Dark background, green accent, large values and restrained labels. Swipe or short
Next cycles the brewer carousel. Only the bottom name/Select confirms selection.
Select activates the visible enabled bottom action; Next has no preparation
action. Press highlights, release executes, and dragging outside cancels.
Hold Next 650ms for Back; hold both keys 900ms for Home, consuming short actions.
Back during brewing requests Stop and waits for the exact event. Back from Ready
requests Exit Ready and waits for its event. Reports use swipes, not short keys.

## Idle shutdown

On every screen, 10 minutes without touch/button interaction or a state change
powers off StopWatch through M5PM1. Page/session, scale phase, connection and
interruption transitions restart the interval. Weight notifications, timer display
updates, rendering and diagnostic status reads do not keep the device awake.

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

## Wi-Fi settings

Only the idle connection screen offers a Wi-Fi setup bottom action. Settings are
a modal screen with a six-digit Bluetooth pairing code, network status and Close
action. Chrome's static setup page writes credentials over authenticated BLE.
The scale may connect behind the modal; closing settings reveals normal brewing
navigation. Wi-Fi is independent of scale/RS measurements and remains on after
setup closes. Detailed behavior and protocol are specified in wifi.md.
