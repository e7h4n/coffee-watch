# Coffee Watch

A small-screen coffee brewing companion for the **M5Stack StopWatch**, **BOOKOO Themis Ultra** scale and **DiFluid R2 Extract** refractometer.

The product follows scale measurements and timer events rather than asking users to manage a recipe wizard. R2 is only needed after beverage weighing.

## Specification and prototype

- [Product specification](spec/product.md): authoritative product behavior and interaction rules.
- [Bluetooth integration](spec/bluetooth.md): documented capabilities, version limits and hardware validation work.
- [Acceptance checklist](spec/acceptance.md): behavior to preserve during implementation.
- [Interactive prototype](spec/prototype.html): latest accepted dark/green UI, with an external hardware simulator.
- [Brewer SVG assets](spec/icons/).

Open `spec/prototype.html` directly in a browser, or serve the repository:

```sh
python3 -m http.server 8000
```

Then open `http://localhost:8000/spec/prototype.html`.

The prototype is self-contained: CSS, JavaScript and brewer SVGs are inline. It needs no build step or network connection. It does **not** connect to Bluetooth or flash hardware. The simulator controls outside the circular device are development tools, not device UI.

This repository currently captures product design, not production firmware. No device firmware, credentials, flash dumps or private user data are included.
