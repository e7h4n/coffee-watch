# Acceptance checklist

- [ ] Scale connects without RS; all four brewers can be selected by swipe/Next.
- [ ] Bottom press/release and Select agree; dragging out cancels.
- [ ] Brewer confirmation initializes Ratio + Auto with a fixed query/exit/tare
      sequence, including a scale left in Ready or the previous result.
- [ ] Empty-server capture starts automatically after initialization, >10g and stable 5 seconds.
- [ ] Saved dose before startup is read without resaving during preparation.
- [ ] Preparation has one Start; exact Ready event opens Ready, not brewing.
- [ ] Pouring triggers brewing; Stop freezes water/time/dose from its event.
- [ ] After Stop, Remove server has no Reset. Negative gross mass stable 1 second
      produces beverage mass after subtracting the saved empty-server mass.
- [ ] Missing packets, drift and disconnect cannot advance stability.
- [ ] Weight/ml and calculation inputs have one decimal, correctly rounded.
- [ ] Only a fresh normal RS result after Waiting TDS produces the report.
- [ ] Numerical report and chart use identical calculated values.
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
