# airspy

Reverse engineering the SKS AIRSPY tyre-pressure sensor, and building a three-wheel
pressure monitor for a **tadpole velomobile** — two front wheels, one rear — on an
ESP32.

The protocol is decoded and written down in [`docs/20-protocol.md`](docs/20-protocol.md).
The monitor in [`airspy_trike/`](airspy_trike/) holds all three sensors at once, warns
below a settable pressure, and warns on a slow puncture. It has not yet been ridden.

## Why

The AIRSPY talks only to SKS's own MYBIKE app over BLE, and to SKS's own Garmin
Connect IQ data field over ANT. Neither handles three wheels in a useful layout, and
neither puts the three readings in front of you while riding. SKS publishes no protocol
documentation, and when this started **nobody had published a single BLE UUID or
advertisement byte layout for the stock firmware**.

In a velomobile the tyres sit largely inside the shell, so a soft tyre is not visible
while riding — and there are three of them to forget to check. The front pair runs at
higher pressure than the rear, and a slow leak in a front wheel is both the most likely
failure and the hardest to notice.

## What is here

- [`docs/20-protocol.md`](docs/20-protocol.md) — **the deliverable**: advertisement
  layout, GATT tree, the 30-byte message, the units, and why the pressure float is the
  sensor chip's own 9-bit code rescaled.
- [`docs/00-project.md`](docs/00-project.md) — goals, vehicle context, requirements.
- [`docs/10-recon.md`](docs/10-recon.md) — what was tried and what was seen, including
  the dead ends.
- [`docs/30-decisions.md`](docs/30-decisions.md) — decision records.
- [`docs/90-references.md`](docs/90-references.md) — every source consulted, including
  the searches that found nothing.
- [`airspy_trike/`](airspy_trike/) — the monitor. One sketch, two boards; the folder is
  named after the sketch because the Arduino build requires it.

## Quick start

    arduino-cli compile --fqbn esp32:esp32:lilygo_t_display airspy_trike
    arduino-cli upload -p <port> \
        --fqbn esp32:esp32:lilygo_t_display:UploadSpeed=115200 airspy_trike

Hold **A** while it starts and it runs on three invented sensors, so the whole thing
can be driven without the vehicle anywhere near. See
[`airspy_trike/README.md`](airspy_trike/README.md) for the layout, the controls and
what each file holds.

To confirm the protocol against your own sensor, `docs/20-protocol.md` §3.6 is the
whole recipe: scan actively for `AIRSPY-<serial>`, connect, subscribe to `0xFFF4`, and
read a `float32` at offset 8 of every notification ending in `0x2E`. The host-side
Python used to find it is not published; it was scaffolding, and the spec is the part
worth having.

## Ground rules

1. **No capture without ground truth.** Every capture gets a note written *during* the
   session with the sensor serial, the floor-pump gauge reading, the temperature, what
   MYBIKE displayed, and what was physically done. A capture without its ground truth
   cannot be decoded later and is worthless. The raw captures themselves are not
   published; the byte layouts they produced are, in the protocol spec.
2. **Label every protocol claim** CONFIRMED / INFERRED / UNKNOWN, with a source.
   Inference is useful; inference mistaken for fact wastes days.
3. **The sensors stay stock.** They are not reflashed. See `docs/30-decisions.md`.
4. **Advisory instrument.** This tells you a tyre is going soft. It does not replace a
   pressure check before a ride, and no feature should imply otherwise.
