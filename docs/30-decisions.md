# Decision records

Short, dated, and honest about what was not known at the time.

---

## ADR-001 — BLE as the protocol, ANT only as a cross-check
**Date:** 2026-10-07 · **Status:** accepted, and borne out

### Context
The AIRSPY transmits on **both** ANT and BLE from one nRF52832. ANT is broadcast
one-to-many: three sensors are trivial, no exclusivity, no connection limits, and
SKS's own Garmin data field uses it exclusively. BLE is connection-oriented and
exclusive — one central per sensor — and completely undocumented for this device.

The original target was a watch whose firmware had no ANT stack, which settled it. That
target has since changed (ADR-004) but the decision did not need revisiting.

### Decision
Pursue **BLE** as the only path that reaches a receiver we can actually build. Try ANT
once anyway, as a guaranteed fallback and as an independent second reading of the same
physical state — two protocols agreeing on 6.00 bar, with a floor-pump gauge as a third
witness, would have been conclusive.

> ANT is the easier protocol and the harder platform.
> BLE is the harder protocol and the one that lands on a device we can program.

### Consequences
- The hard, open-ended work — an undocumented protocol — was on the critical path.
  Accepted deliberately, and it worked: no bonding, no handshake, no checksum.
- The cross-check turned out to be unnecessary, because the BLE decode confirmed itself
  from the inside: the pressure float resolves to the sensor chip's own 9-bit code, and
  values that land on that lattice cannot be a coincidence of scaling (§3.4).
- ANT is therefore **documented but not pursued**. See `20-protocol.md` §2.

---

## ADR-002 — Do not reflash the sensors
**Date:** 2026-10-07 · **Status:** accepted

### Context
`bitmeal/sks_airspy_ant_community_fw` offers alternative firmware turning the AIRSPY
into an ANT+ TPMS transmitter. It is well documented and is the source of our internal
SPI knowledge.

### Decision
The sensors stay on stock firmware.

### Rationale
- The community firmware **removes BLE and the MYBIKE app entirely** — it is ANT-only.
  Flashing it would foreclose the primary path chosen in ADR-001 before we have even
  measured it.
- Flashing is **effectively one-way**: stock DFU payloads are signed with a key only SKS
  holds, so there is no documented route back to stock firmware.
- Reflashing destroys the reference implementation. MYBIKE on the iPhone is our ground
  truth for every decode; we need it working.

### Revisit if
BLE proves unusable (ADR-001 fallback) *and* the chosen receiver speaks ANT. Then
community firmware becomes attractive — it is interoperable with standard ANT+ TPMS
displays, which stock firmware is not. Flash **one** sensor first, never all three.

---

## ADR-003 — Capture discipline: no capture without ground truth
**Date:** 2026-10-07 · **Status:** accepted

### Context
Decoding an unknown binary payload means correlating bytes against known physical
values. A capture whose physical state was not recorded cannot be correlated against
anything, no matter how good the bytes are.

### Decision
Every capture gets a note written during the session, recording
sensor serial, floor-pump gauge reading,
temperature, MYBIKE's displayed values, and what was physically done.

### Consequences
Slightly tedious during a session; the difference between a decodable archive and a
pile of hex. Captures are committed to the repo — they are small, and they are the
golden vectors the decoder's tests run against.

---

## ADR-004 — The receiver is an ESP32, not a watch
**Date:** 2026-10-16 · **Status:** accepted

### Context
The project began aimed at a Bangle.js 2, on the reasoning that a device already on the
wrist costs nothing extra to carry. Two things undid that. The protocol turned out to
need a *connection per sensor*, and holding three at once is the difference between one
glance and a round-robin that is always showing one stale wheel. And a warning device
that shares a battery with a watch has to choose between warning and lasting.

### Decision
Put the monitor on the handlebar on an ESP32 — a LilyGO T-Display — and power it from
the vehicle's own battery. One sketch covers that board and the T-Display-S3.

### Consequences
- Three simultaneous connections, which NimBLE supports by default. No polling, no
  stale wheel, and the three readings can be laid out the way the vehicle looks from
  above.
- Powered means running: no deep sleep, no automatic disconnect. At 20 mA on a vehicle
  battery there is nothing to save that is worth missing an event for, and the plug is
  the off switch.
- Something else to mount and wire, and one more thing that is dead if the vehicle's
  battery is.
- Espruino, the Bangle.js central-link limit, and the custom-firmware route are all
  moot. Removed from the references rather than left to mislead.

---

## Gates, and how they closed

### Gate 1 — is BLE viable, and in what shape? **Closed: yes, by connection**
Pressure is reachable over BLE per sensor, repeatably, but **not** in the
advertisement — it needs a connection, which decided the receiver's whole shape. The
sensor does not require pairing and streams unprompted at 2 Hz once notifications are
enabled.

### Gate 2 — receiver architecture **Closed: all three at once**
The question was advertisement-only versus round-robin polling versus custom firmware.
Gate 1 ruled out the first. The third disappeared with ADR-004: an ESP32 holds three
links by default, so the monitor keeps all three sensors connected and none of the
readings is ever a rotation behind.

### Gate 3 — does the sensor bond exclusively to one central? **Open**
Connecting needed no pairing, and whether MYBIKE bonds is still untested. It matters
only as a convenience question — whether the phone app and the monitor can be used in
the same session, or whether they are an either/or. Nothing in the monitor depends on
the answer.

### Gate 4 — does it work on the road? **Open**
The one that is left. Slow-puncture detection and its temperature compensation have
never met a real tyre over a real ride, and the RF survey inside the shell
(`10-recon.md` §6) has not been done.
