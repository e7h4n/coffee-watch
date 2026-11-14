# Visual system

## Surface and layout

Use the 468 x 466 RGB565 framebuffer with a centered round 466px viewing area.
Main content is centered at (234,233). Keep text clear of the round edge. The
bottom action starts at y=354 and retains the same hit area on every page.
Do not move hit targets during entry animations. Connect scale is vertically
centered at screen y=233. Bottom actions occupy y=354..465, with label center
y=410. Both use visible glyph bounds for vertical centering, excluding font
ascent/descent padding; these positions are shared tokens, not page offsets.

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
  It arms itself, so `Pour to start` (26) sits above it at y=124, Ready (60) at
  y=190, and Dose / Bed labels (22) at y=266 with 40px values at y=300.
- Brew page (the scale display no longer shows added water, so the watch does):
  water as the 90px hero at (222,124) with `ml` (26) after it; Ratio and Bed
  labels (22) at y=194 over 40px values at y=228, the ratio in accent and the bed
  temperature with a drawn degree ring (fonts are ASCII); a compact live chart at
  (88,264)-(380,334); the brew clock (64) at y=396 in the free bottom of the round
  screen, below the battery-only top. The chart band keeps the fixed 60-100 °C
  grid, minute grid, added-water steps (#223028 with a #55685c edge, scaled to
  max(300 ml, 1.15 × water)), a 3px accent curve and its end dot, with no axis,
  ml or time labels. Water changes with every notification while pouring, so it
  never value-flashes. The chart and clock redraw at the 10 Hz trace rate and there
  is no bottom action while brewing.
- Stopped brew (revisited after the dripper lift): the same water and ratio,
  Dose instead of Bed, the frozen brew time in accent at y=296, and Continue as the
  bottom action; no chart.
- Brew curve result page: the same chart drawn from the frozen trace at
  (88,118), 304×200px, without the end dot, with 70/80/90/100 and minute labels and
  the water labeled in ml; the accent time label marks the end of the brew. No
  title or hint text. Both charts span 2:30, then 4:00, 6:00 and further two-minute
  steps; the curve compresses rather than scrolls. Readings below 60 °C enter from
  the bottom edge; missing readings break the line.
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
