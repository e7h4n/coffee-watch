# Product behavior

## Hardware and flow

M5Stack StopWatch (468×466 framebuffer), BOOKOO Themis Ultra 4.0.1 in Ratio
mode (manual or Auto submode; the watch never arms Auto), the bed probe, and
DiFluid RS v002-005. Brewers: V60, Switch, AeroPress and Hoop. The complete
brewing assembly sits on the scale while pouring.

1. Connect the scale and select a brewer. RS is not needed yet.
2. Place an empty server. Capture weight >10g stable within 0.2g for 5 seconds.
   Optional Reset dispatches tare; ATT success never claims tare completion.
3. Prepare the brewing assembly. Dose is saved on the scale during bean weighing,
   possibly before watch startup. Read its stored dose at connection; no dose
   selection or resaving during preparation.
4. There is no Start button. Once the empty server and the stored dose are known,
   preparation arms the watch's own pour detection by itself and shows Ready;
   without a dose it shows Save dose on scale and never arms. Nothing is sent to
   the scale. Arming restarts the temperature baseline, so earlier probe rises (a
   rinse, a warm hand) never count. The brew starts when the bed probe reads at least 3.0 °C above the median
   of its previous 3 seconds and, within 1.5 seconds, the weight is at least 0.5g
   above the zero. The zero is the median of the latest plateau (at least 1 second,
   within 0.2g, no gap over 1 second) inside the 5 seconds before the probe
   trigger, so water already poured is counted. Brew time starts at the first
   weight 0.3g above that zero. A pour without such a plateau is an explicit
   interruption, never a guessed zero. Missing probe readings sit below the
   ~25 °C ADC floor, so a mostly-missing window uses 25 °C as its baseline.
5. The scale's own display is gross weight now, so the watch shows the brew: added
   water (live weight minus the zero) largest, then the ratio and the live
   bed-probe temperature, a compact temperature curve with the water steps behind
   it, and the brew time at the bottom. There is no Stop action on the watch while brewing: lifting the
   dripper ends the brew. The brew ends when a settled level (1 second within
   0.2g) is followed by a new settled level at least 30.0g lower. Water is the
   last settled level before the lift minus the zero; brew time ends with that
   level. Stirring, valve spikes and presses never settle, so they never end it.
   Back discards a live brew (for example a false start from rinsing the paper
   with the probe in the dripper) and Ready re-arms at once. A brew revisited
   after the lift shows Continue back to beverage weighing.
6. The server stays on the scale after the lift. Beverage mass equals live weight
   minus the saved empty-server mass. Require positive beverage and <=0.2g
   variation for 1 second. No Reset and no server removal are needed.
7. Connect RS and accept a new normal measurement. Show numerical report and
   Golden Cup chart, using the same result.
8. Result pages: Waiting TDS shows its summary and the brew curve; the report
   shows the numerical report, the Golden Cup chart and the brew curve. Pages
   advance every 5 seconds and wrap; a swipe or short Next turns the page and
   restarts that interval. Page turns are display-only and never count as
   activity for idle shutdown. The curve page is the frozen probe trace (the
   data in the Dropbox CSV, from the pour to Stop) and exists once the trace
   reached the brew start.

## Precision and calculation

Mass and volume displays and calculation inputs use one decimal place, rounded
half away from zero. The wire decoder retains centigrams. TDS retains sensor
precision and is displayed to two decimals. Water ml uses the scale's gram value.

- Brew ratio = added water / dry dose.
- Extraction percent, V60/Hoop = TDS percent × beverage mass / dry dose.
- Extraction percent, Switch/AeroPress = TDS percent / (100 - TDS percent) ×
  added water / dry dose × 100. Switch is defined as a valve-closed steep
  followed by a full drain; AeroPress as an inverted or outlet-closed steep
  followed by one full press, without bypass dilution. Retained brew in the
  grounds is at cup strength and counts as extracted.
- Retention g/g = (added water - beverage mass) / dry dose.

No assumed retention, default dose, manual TDS or offline measurement path.

## Events and continuity

Business transitions follow validated notifications from the active connection
and the bed probe.
Weight stability is evaluated only on weight notifications; gaps >1 second reset
continuity. UI rendering and elapsed timers cannot complete an operation. There
are no command retries, polling completion checks or fallback measurements.
Disconnect interrupts active brewing. Explicit Home begins a new session;
reconnection alone never resumes it. New connections invalidate the dose cache.
Home/navigation within the same connection preserve scale-reported stored dose.
The pour start freezes its own dose snapshot; later dose changes cannot alter a result.
Only a fresh, valid RS result after entering Waiting TDS creates a report.

## Input and appearance

Dark background, green accent, large values and restrained labels. Swipe or short
Next cycles the brewer carousel. Only the bottom name/Select confirms selection.
The carousel starts at the last confirmed brewer (kept in NVS across reboots and
through Home).
Select activates the visible enabled bottom action; Next has no preparation
action. Press highlights, release executes, and dragging outside cancels.
Hold Next 650ms for Back; hold both keys 900ms for Home, consuming short actions.
Back during brewing discards the brew and re-arms Ready; Back from Ready returns to
empty-server capture. Neither sends a scale command. Result pages turn by swipe or short Next.

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
A single mode query (0F -> 0E) must report Ratio (byte 2 = 2), the mode that holds
the stored dose. Any other mode is an explicit interruption. If the Ratio submode
is Auto (byte 5 = 2), a fixed single-pass sequence dispatches Exit Ready (18/01)
and Exit Done (07) first, so a scale left in Ready or Done returns to Idle. Tare
(01) follows in every case. Each write is sent once, serialized by its ATT result;
there is no retry or alternate sequence. ATT success is dispatch status, never a
tare-completed assertion. Only subsequent actual weight notifications can complete
the independent 5-second empty-server capture rule. Mode query or write failure
cannot advance capture.

The watch never arms the scale's Auto submode. An Auto event (03 0D) during
preparation, Ready, brewing or beverage weighing means the scale re-tared itself,
which would move the zero under the brew, so it is an explicit interruption.

## Wi-Fi settings

Only the idle connection screen offers a Wi-Fi setup bottom action. Settings are
a modal screen with a six-digit Bluetooth pairing code, network status and Close
action. Chrome's static setup page writes credentials over authenticated BLE.
The scale may connect behind the modal; closing settings reveals normal brewing
navigation. Wi-Fi is independent of scale/RS measurements and remains on after
setup closes. Detailed behavior and protocol are specified in wifi.md.
