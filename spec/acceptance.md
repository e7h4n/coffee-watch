# Acceptance checklist

- [ ] Scale connects without RS; all four brewers can be selected by swipe/Next.
- [ ] Bottom press/release and Select agree; dragging out cancels.
- [ ] Brewer confirmation requires Ratio mode and tares once; in the Auto submode it
      first exits a scale left in Ready or the previous result.
- [ ] Empty-server capture starts automatically after initialization, >10g and stable 5 seconds.
- [ ] Saved dose before startup is read without resaving during preparation.
- [ ] Preparation has no Start: with the dose known it shows Ready by itself, without
      a scale command; without a dose it never arms.
- [ ] Pouring hot water on the bed probe starts brewing; water poured before the
      trigger is counted and the timer starts at the weight onset.
- [ ] A warm hand, stirring and valve opening never start or stop a brew; a rinse
      false start is discarded by Back and Ready re-arms; the scale's Auto key
      interrupts it.
- [ ] Lifting the dripper freezes water/time/dose; with the server left on the
      scale, a stable second produces beverage mass minus the empty server.
- [ ] Missing packets, drift and disconnect cannot advance stability.
- [ ] Weight/ml and calculation inputs have one decimal, correctly rounded.
- [ ] Only a fresh normal RS result after Waiting TDS produces the report.
- [ ] Numerical report and chart use identical calculated values.
- [ ] Result pages advance every 5 seconds; swipe/Next turns and restarts the
      interval; the curve page matches the uploaded trace CSV.
- [ ] After a reboot or Home the carousel starts at the last confirmed brewer.
- [ ] Switch and AeroPress report immersion extraction from added water; V60 and
      Hoop report beverage-based extraction.
- [ ] Back/long Home work; key chords consume short presses.
- [ ] Reconnect never resumes an interrupted brew automatically.
- [ ] Serial `probe` reads about 3.0 V on G8 at room temperature and a falling
      G8 voltage with rising probe temperature; 90 °C water reads 88–92 °C.
- [ ] Start → pour → Stop produces `brew-<id>-trace.csv` at 10 Hz, `t_ms` 0 at
      the pour, and the record's `probe_trace` points to it.

Automated checks run with `python3 -m unittest discover -s tools -v`.
Physical checks are a checklist, not an assertion that every release passed them.
