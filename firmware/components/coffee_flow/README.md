# Coffee flow model

Portable, event-driven business state machine shared by the real demo and native
tests. It has no timer task, retry path, alternate sensor source or default dose.

`cw_scale_message` accepts validated BOOKOO data only in the current connection
epoch. New brewer confirmation begins the brew and retains the scale-reported stored
dose of the current connection. UI navigation and Home never ask for resaving it.
Disconnect invalidates dose availability until the scale reports again.
The single preparation Start writes Auto Ready (18/00), confirmed only by Ready
event 02. A later real Start event enters brewing; there is no manual timer-start
button in this product flow. Stop advances only on its exact Auto event. Stop's own dose snapshot,
water and elapsed time are immutable throughout weighing and reporting.

Empty-server capture requires >10g stable within 0.2g for 5 seconds. After
confirmed Auto Stop, `Remove server` waits for live weight < -30.0g and positive
beverage = -live weight - empty server. Stability within 0.2g for 1 second freezes
beverage. These are notification-driven primary rules: gaps >1 second restart
continuity; no external tick can complete them. Mass inputs are rounded half away
from zero to 0.1g before calculation, matching displayed precision.

Reset is available only for empty-server weighing. It dispatches tare without
claiming completion from ATT success. There is no final-weighing Reset command.
Opcode 07 exits confirmed Auto Done and tares; otherwise opcode 01 is selected.

Entering Waiting TDS records an observation boundary. Only a later valid normal
RS result in the active connected session can create a report. The demo opens RS
only at this stage and filters its connection epoch. Error/other/stale results
never count as a concentration. Native model has no fake-data input path in the
deployed console. Formula uses measured beverage mass, not added water or a
retention assumption.

Home invalidates captured data and pending callback generation. Scale loss
interrupts active brewing and clears continuity; reconnect alone cannot resume.
Back during brewing sends Stop through the normal command interface and waits for
that same Stop event. Back can revisit already frozen evidence; revisiting is
navigation, not a second Stop result. Returning to preparation begins a new brew while retaining the current
connection's explicit scale-stored dose. Ready Back uses 18/01 and waits for Exit
Ready event 03.

Native test suite: 10 scenarios including complete measurement->report arithmetic,
stale/wrong epoch, thresholds/gaps/drift, event-only command completion, missing
dose, interrupted state, negative removal/gaps, frozen-result revisiting and Back waiting
for Stop. Tests use explicit fixture events outside the deployed firmware.

Brewer confirmation starts a one-pass initialization transaction. A validated
0E snapshot must confirm Ratio + Auto, then Exit Ready / Exit Done / tare are
dispatched in order, once each. ATT status serializes these writes but does not
assert tare completion. Empty-server capture remains blocked until the writes
finish, then requires its normal actual-notification stability window. Bad mode
or any write failure interrupts the transaction without retries.
