# Coffee flow model

Portable, event-driven business state machine shared by the real demo and native
tests. It has no timer task, retry path, alternate sensor source or default dose.
Brew start, zero, water, time and beverage are watch-owned: they come from BOOKOO
weight notifications and the bed probe, never from the scale's Auto submode.

`cw_scale_message` accepts validated BOOKOO data only in the current connection
epoch; `cw_probe_message` takes each 10 Hz bed-probe reading. New brewer
confirmation begins the brew and retains the scale-reported stored dose of the
current connection. UI navigation and Home never ask for resaving it. Disconnect
invalidates dose availability until the scale reports again.

Empty-server capture requires >10g stable within 0.2g for 5 seconds. There is no
Start: once the stored dose is known, preparation arms itself (Ready) without a
scale command, restarting the temperature baseline but keeping weight history.
`pour.c` keeps the last 128 weights and 64 probe readings. While Ready, a probe
reading 3.0 °C above the median of its previous 3 seconds triggers, and the brew
starts once the weight is 0.5g above the zero within 1.5 seconds. The zero is the
median of the latest plateau (>=1 second, <=0.2g, no gap >1 second) in the 5
seconds before the trigger, so water poured before the probe reacted still counts.
The onset, where brew time starts, is the first weight 0.3g above the zero. Water
without such a plateau interrupts with `No stable weight before pour`. Triggers
before arming never count. A cold probe reads as missing below the ~25 °C ADC
floor; a window of mostly missing readings uses 25 °C as its baseline.

While brewing, a settled level (1 second within 0.2g) followed by a new settled
level at least 30.0g lower is the dripper lift. Water is the last settled level
before it minus the zero, and brew time ends with that level; the pour start's own
dose snapshot, water and time are then immutable. The server stays on the scale:
beverage = live weight - empty server, positive and stable within 0.2g for 1
second. These are notification-driven primary rules: gaps >1 second restart
continuity; no external tick can complete them. Mass inputs are rounded half away
from zero to 0.1g before calculation, matching displayed precision.

Reset is available only for empty-server weighing. It dispatches tare (01) without
claiming completion from ATT success. There is no final-weighing Reset command.

Entering Waiting TDS records an observation boundary. Only a later valid normal
RS result in the active connected session can create a report. The demo opens RS
only at this stage and filters its connection epoch. Error/other/stale results
never count as a concentration. Native model has no fake-data input path in the
deployed console. Formula uses measured beverage mass, not added water or a
retention assumption.

Home invalidates captured data and pending callback generation. Scale loss
interrupts active brewing and clears continuity; reconnect alone cannot resume.
Back acts at once: a live brew is discarded and Ready re-arms; Back from Ready
returns to empty-server capture. Back can revisit already frozen evidence; revisiting is navigation,
not a second result. Back from Waiting TDS re-weighs the server, which never left
the scale. Returning to preparation retains the connection's scale-stored dose.
Home keeps the brewer carousel position.

Brewer confirmation starts a one-pass initialization transaction. A validated 0E
snapshot must confirm Ratio mode (the stored dose lives there). In its Auto
submode, Exit Ready and Exit Done are dispatched first; tare follows in every
case, once each. ATT status serializes these writes but does not assert tare
completion. Empty-server capture remains blocked until the writes finish, then
requires its normal actual-notification stability window. Bad mode or any write
failure interrupts the transaction without retries. Any Auto event (03 0D) during
preparation, Ready, brewing or beverage weighing interrupts: Auto re-tares the
scale on its own and would move the zero.

Native test suite: 20 scenarios including complete measurement->report arithmetic,
water already in at the trigger, a bump before the pour, a pour without a plateau,
warm-hand and rinse triggers, a cold probe, Auto interference, stirring and valve
spikes, sparse notifications, beverage with the server kept on, Back navigation,
missing dose, stale/wrong epoch, thresholds/gaps/drift and mode initialization.
Tests use explicit fixture events outside the deployed firmware.
