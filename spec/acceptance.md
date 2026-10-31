# Acceptance checklist

- [ ] Scale connects without RS; all four brewers can be selected by swipe/Next.
- [ ] Bottom press/release and Select agree; dragging out cancels.
- [ ] Empty-server capture starts automatically, >10g and stable 5 seconds.
- [ ] Saved dose before startup is read without resaving during preparation.
- [ ] Preparation has one Start; exact Ready event opens Ready, not brewing.
- [ ] Pouring triggers brewing; Stop freezes water/time/dose from its event.
- [ ] After Stop, Remove server has no Reset. Negative gross mass stable 1 second
      produces beverage mass after subtracting the saved empty-server mass.
- [ ] Missing packets, drift and disconnect cannot advance stability.
- [ ] Weight/ml and calculation inputs have one decimal, correctly rounded.
- [ ] Only a fresh normal RS result after Waiting TDS produces the report.
- [ ] Numerical report and chart use identical calculated values.
- [ ] Back/long Home work; key chords consume short presses.
- [ ] Reconnect never resumes an interrupted brew automatically.

Automated checks run with `python3 -m unittest discover -s tools -v`.
Physical checks are a checklist, not an assertion that every release passed them.
