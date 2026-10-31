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

Fonts are OFL-licensed Noto Sans Medium, rendered as grayscale-antialiased VLW
assets at native pixel sizes. No fractional scaling or magnification of monochrome
bitmaps is used. `tools/make_fonts.py` reproduces the assets from the bundled font.
Use native sizes 18, 20, 22, 26, 32, 34, 40, 46, 60, 64 and 90. Select the largest
available size not exceeding the role size; step down whole font sizes to fit
hero values (260px) and report columns (100px). Sizes 60/64/90 contain only the
Ready/time/number glyph subsets used by their respective roles.

Hero numbers use 90; Ready 60; report time 64; TDS/extraction 46; primary status
40; labels 22; captions 26; actions 34. Mass and volume remain at one decimal;
animation never interpolates a measurement.

## Battery telemetry

Every page displays a battery outline and percentage at the top center, y=31.
Reserve y=20..44; progress is displayed below the main number.
Following the official StopWatch UserDemo, sample M5PM1 `readVbat` every second,
filter voltage with 7/8 old + 1/8 new and rounded division, then map 3300..4200mV
linearly to 0..100%, clamped. This is a voltage-based estimate, not a fuel gauge.
Read errors show --% instead of inventing a value. Battery updates do not change
business state or reset the 10-minute idle timer. No other device is queried.

## Components

- `text` / `fmt`: consistent font selection, alignment and entry treatment.
- `field`: label and value pair for TDS and reports.
- `bottom`: shared press treatment, enabled state and fixed action region.
- `highlights`: identical TDS/extraction pair on numerical and chart reports.
- Stability bar: 216×12px capsule at (126,306), gray #4b5350 track and green
  elapsed portion. Both ends use analytic antialiasing at native resolution.
  Its progress comes exclusively from the notification-driven business window.
- Connection, initialization and TDS waits use their text status without spinners.
- Ready: subtly breathing text color; no outline, fabricated progress or countdown.
- Brewer carousel: cyclic positioning, scaling and easing with queued input.

## Motion

Rendering is capped at about 30 FPS, not a guarantee of measured throughput.
Animations request redraws only while active; all clocks are display-only.

| Motion | Duration | Behavior |
| --- | --- | --- |
| Page entry | 240ms | Cubic ease-out, text fades in and rises 12px |
| Value change | 180ms | Brief accent tint; exact number changes immediately |
| Carousel | 380ms | Cubic ease-out movement and scaling |
| Ready breathing | 2400ms | Subtle text-color cycle |

No animation delays commands, completes operations, creates measurements, or
changes the idle timer. Stability windows use real notifications only. An
unknown wait never presents a percentage. Missing events stay pending until an
explicit action or the global idle power-off policy.
