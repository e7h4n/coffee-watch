# Brew records

A record is generated from two accepted sensor transitions:

1. **Beverage → Waiting TDS**: the confirmed beverage weight creates a JSON
   record in the watch's authorized **Coffee Watch** folder, preceded by the
   brew's 10 Hz probe trace CSV (see [Probe trace](#probe-trace)). Its status is
   `awaiting_tds`; TDS, extraction, sample temperature and refractive index are
   explicitly `null`.
2. **Waiting TDS → Report**: an accepted RS measurement updates the same Drive
   file to `complete` with those measurement fields. A real zero TDS is `0.00`,
   not `null`.

The normal event/epoch and stability checks remain in the brewing state machine.
Raw weight notifications, stale RS results, page rendering, navigation to the
chart and repeated measurements after the report do not generate uploads.
Returning to beverage weighing updates the existing brew identity when the
weight is confirmed again. A new confirmed Ready → Brew transition allocates a
new identity, including a device/session nonce so IDs are distinct across boots.

## Serialization and identity

Each queued snapshot is immutable and includes an increasing revision. The one
Drive worker executes snapshots in FIFO order: a fast TDS measurement cannot
replace or overtake the initial creation. Records from different brews retain
separate IDs even if uploads overlap a later brew. Older upload results cannot
replace the status of a newer revision or a newer brew on the watch.

For each upload, the worker loads the saved authorization, synchronizes the
clock if necessary, refreshes an access token and validates/resolves the
authorized folder. Revision 1 creates the JSON file. The worker retains Google's
returned file ID and the record's UTC timestamp, then uses that exact ID for
subsequent PATCH operations. It validates the file's parent and trash status
before updating. There is no filename search, status polling or alternate
creation route for a missing draft. The `coffeeWatchRecord` app property also
identifies the record for external readers; it is not a substitute for the
create response. The connection-check file is never used as a brew record.

Accepted record events for one brew precede all events of the next brew, and
the FIFO worker preserves that order. The retained binding is replaced only by
the next brew's initial draft. A credential change invalidates the binding for
updates; it cannot redirect an old brew into a newly authorized account.

The initial `created_at` is the beverage event's monotonic timestamp converted
to UTC after clock synchronization. It is retained with the file ID, also stored in the file's `recordedAt` app
property, and reused unchanged by subsequent revisions. Filenames are
`brew-<record-id>.json`. Drive's normal file creation/modification metadata also
remains available.

Only a successful upload followed by an exact content read-back reports
**Drive: saved**. The UI shows saving/saved/save-failed on Waiting TDS and the
report/chart. Network work never blocks sensor processing or the user interface.
Missing authorization, network, lookup, create, PATCH and read-back failures are
explicit. There is no automatic retry, periodic sync check, offline replay or
alternate storage/upload path. Brew measurements remain available in the local
workflow when an upload fails. A reboot resets that workflow; already-created
Drive drafts remain in Drive even if TDS was never measured.

## Probe trace

From the moment Start is accepted (Ready) until the brew stops (Brew →
Beverage), a 10 Hz task samples the latest scale weight and the bed-probe
temperature. The probe is a 100 kΩ B3950 NTC on the StopWatch back header: 3V3
→ 10 kΩ → G8 → NTC → GND, with G9 = 3V3/2 from two 10 kΩ resistors. Both are
read through ADC1 (12 dB, eight interleaved samples each); the G8/G9 ratio
cancels supply and reference drift. A saturated, open or shorted probe is a
missing value, not a temperature. Readings below roughly 25 °C are near the ADC
range limit and may be missing.

The trace holds up to 20 minutes (12,000 samples); later samples are dropped and
the trace is marked truncated. Returning from Ready to Preparation and starting
again discards it. With the first draft of a brew, the Drive worker uploads it as
`brew-<record-id>-trace.csv`:

```text
t_ms,weight_g,probe_c
-1200,0.0,93.12
-1100,0.0,93.10
0,0.3,93.05
100,1.8,92.98
```

`t_ms` is relative to the scale timer start (the Ready → Brew event), negative
while waiting for the pour. Weight has one decimal like the rest of the product;
temperature has two decimals. Missing values are empty fields. Upload is
verified by comparing Drive's `md5Checksum` and `size` with the local CSV, then
the record JSON references the file. A failed trace upload fails the draft; no
record is created without the trace it describes. A record whose creation fails
after the trace upload can leave that CSV in the folder. Later revisions repeat
the same `probe_trace` object and do not upload the CSV again.

## Schema v3

Mass/volume fields use the product's 0.1g/ml rounding before calculations.
The ratio has one decimal place, retention and extraction have two, TDS has two,
sample temperature has one and refractive index has five. `elapsed_ms` preserves
the scale's frozen brew duration. TDS and extraction match the existing report.

`extraction_formula` names how `extraction_percent` was calculated and is present
in drafts as well: `"immersion"` for Switch (valve-closed steep, then full drain)
and AeroPress (inverted or outlet-closed steep, one full press, no bypass
dilution), calculated as TDS / (100 − TDS) × added water / dry dose × 100; and
`"beverage"` for V60 and Hoop (TDS × beverage mass / dry dose). The beverage-based value of
any record can still be recomputed from `tds_percent`, `beverage_g` and `dose_g`.
Schema v1 records have no `extraction_formula` and always used the beverage
formula. Schema v3 adds `probe_trace`; it is `null` only if no trace was
recorded for the brew.

```json
{
  "schema_version": 3,
  "id": "020000000001-0123456789abcdef-00000001",
  "revision": 1,
  "created_at": "2026-11-02T08:00:00Z",
  "status": "awaiting_tds",
  "brewer": "Switch",
  "dose_g": 15.0,
  "water_ml": 225.0,
  "beverage_g": 196.5,
  "empty_server_g": 298.0,
  "elapsed_ms": 180000,
  "brew_ratio": 15.0,
  "retention_g_per_g": 1.90,
  "extraction_formula": "immersion",
  "tds_percent": null,
  "extraction_percent": null,
  "sample_temperature_c": null,
  "refractive_index": null,
  "probe_trace": {
    "name": "brew-020000000001-0123456789abcdef-00000001-trace.csv",
    "file_id": "1AbCdEfGhIjKlMnOpQrStUvWxYz",
    "sample_rate_hz": 10,
    "samples": 2104,
    "truncated": false,
    "columns": ["t_ms", "weight_g", "probe_c"]
  }
}
```

The TDS update preserves `id`, `created_at` and Drive file ID, advances
`revision`, sets `status` to `complete`, and fills the four measurement fields.
No Wi-Fi password, OAuth client credential, access token or refresh token is
included in a record.
