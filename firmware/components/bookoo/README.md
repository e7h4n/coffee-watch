# BOOKOO protocol and session adapter

Portable C11 core without BLE, screen or allocation dependencies. Wire values
are centigrams; the product model rounds them to 0.1g before calculation.
Supported baseline: Themis Ultra 4.0.1, Ratio + Auto.

Transport routes validated FF11 and FF12 notifications for service 0FFE into the
adapter with a connection epoch. New connections invalidate cached dose. New
brews within one connection retain the explicit scale-reported stored dose.
03 10 query response and 03 0F changes update the cache. Start freezes dose for
the run; Stop freezes dose, water and elapsed time for its immutable result.

Ready, Exit Ready, Start and Stop requests leave state unchanged until the
matching 03 0D event. Only one request can be pending. ATT success, elapsed time
and ordinary weight never complete commands. No retries, timeouts or polling
completion. Transport failure clears the pending request explicitly.
Ready/Start elapsed fields may belong to the previous brew and are ignored.

Disconnect invalidates live/dose availability and interrupts active progress;
reconnection alone never resumes it. The product must explicitly start a new
session. Tare is dispatch-only; no near-zero synthetic completion event exists.

See spec/bluetooth.md for the exact interfaces. Native fixtures exercise packet
validation, event-confirmed commands, stored-dose lifecycle, interruption,
immutable results and server-removal settlement.
