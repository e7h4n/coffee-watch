# Coffee Watch

Coffee brewing companion for M5Stack StopWatch, BOOKOO Themis Ultra 4.0.1
(Ratio + Auto), and DiFluid RS v002-005.

The watch captures empty-server mass, arms Auto Ready, follows exact brewing
events, measures beverage by server removal, and receives a fresh TDS result for
its numerical report and Golden Cup chart.

- [Build, flash and walkthrough](firmware/demo/README.md)
- [Product behavior](spec/product.md)
- [Bluetooth interfaces](spec/bluetooth.md)
- [Acceptance checklist](spec/acceptance.md)
- [Brewer artwork](spec/icons/)
- [Browser design reference](spec/prototype.html): historical visual prototype,
  with simulated hardware. Its workflow predates the native firmware.

From the repository root, run protocol and native state-machine tests:

```sh
python3 -m unittest discover -s tools -v
```

Requires a C compiler; the native AES harness uses macOS CommonCrypto. Firmware
uses ESP-IDF mbedTLS. Build outputs, downloaded drivers and local captures are
excluded from Git.
