# Visual system

## Surface and layout

Use the 468 x 466 RGB565 framebuffer with a centered round 466px viewing area.
Main content is centered at (234,233). Keep text clear of the round edge. The
bottom action starts at y=354 and retains the same hit area on every page.
Do not move hit targets during entry animations.

Colors and typography live in `firmware/demo/main/design.h`. Background #141c18,
foreground #e9eee8, secondary #8d9e93, accent #c9f27b, divider #334038, button
#26352a. Accent identifies primary actions, results and relevant activity.

## Typography

Sizes are renderer text sizes, not measured cap heights. M5GFX FreeSans fonts
are shared across all pages. Use a maximum of two text weights/fonts.

| Role | Size | Use |
| --- | --- | --- |
| Hero number | 90 | Weight and added water |
| Ready | 60 | Armed waiting state |
| Time | 64 | Report brew time |
| Result | 46 | TDS and extraction |
| Status | 44 | Connection page |
| Metric | 36 | Secondary values; adapt to the available column width |
| Body | 26 | Weighing and water captions |
| Label | 22 | Metric labels |
| Action | 34 | Shared bottom button |

Keep mass and volume at one decimal. Never interpolate sensor values for a
rolling-number effect: displayed values must retain their product precision.
Hero values are constrained to 260px and report column values to 100px; text
scales down when necessary to fit. Long values must also be checked against
column width on the physical framebuffer.

## Components

- `text` / `fmt`: consistent font selection, alignment and entry treatment.
- `field`: label and value pair for TDS and reports.
- `bottom`: shared press treatment, enabled state and fixed action region.
- `highlights`: identical TDS/extraction pair on numerical and chart reports.
- Stability ring: progress from the notification-driven business window.
- `waiting_mark`: small rotating arc for connection and TDS waiting.
- Ready ring: subtle breathing outline; no fabricated progress or countdown.
- Brewer carousel: cyclic positioning, scaling and easing with queued input.

## Motion

Rendering is capped at about 30 FPS, not a guarantee of measured throughput.
Animations request redraws only while active; all clocks are display-only.

| Motion | Duration | Behavior |
| --- | --- | --- |
| Page entry | 240ms | Cubic ease-out, text fades in and rises 12px |
| Value change | 180ms | Brief accent tint; exact number changes immediately |
| Carousel | 380ms | Cubic ease-out movement and scaling |
| Ready breathing | 2400ms | Low-contrast outline cycle |
| Waiting arc | 1200ms | One full revolution |

No animation delays commands, completes operations, creates measurements, or
changes the idle timer. Stability windows use real notifications only. An
unknown wait never presents a percentage. Missing events stay pending until an
explicit action or the global idle power-off policy.
