# Recon log

What was tried and what was seen — including the dead ends, which narrow the search as
much as the successes do. The protocol that came out of it is in `20-protocol.md`; this
file is how it was found.

Two standing practicalities, learned the hard way:

- **Shake the sensor or spin the wheel first.** It sleeps about ten minutes after
  motion stops, and a sleeping sensor looks exactly like a broken one.
- **Record ground truth during the session, not afterwards.** A payload without the
  gauge reading and the timestamp that belongs to it cannot be decoded later. This was
  honoured imperfectly, which is the gap ADR-003 exists to prevent. The raw captures
  are not published; what they established is in `20-protocol.md`, byte by byte.

---

## 1 — Is pressure in the BLE advertisement? **No**

The decisive question. Broadcast pressure would mean no exclusivity, no connection
limit, no sensor-count limit, and a receiver that is one scan callback.

**Method.** A host-side scanner logged every advertisement to JSONL while one tyre was bled
from roughly 6 bar to 2 bar and pumped back up.

**Result.** The payload was **constant across 41 advertisements** while air was
actively escaping. Pressure is behind a connection.

Two things fell out of it anyway. The advertised local name is `AIRSPY-<serial>`,
matching the number laser-engraved on the housing, which is how a sensor is identified
at all. And the manufacturer-data company ID is `0x5253` — ASCII `"SR"`, squatted, not
assigned — followed by `"TMPS-1111-V1.0"` and the MAC.

**Read it with an ESP32, not with macOS.** CoreBluetooth hides the BD_ADDR and hands
over parsed AD structures rather than raw bytes. The AIRSPY's 49 bytes of
advertisement data do not fit the 31-byte limit, so the name lives in the scan
response and only an *active* scan ever sees it.

## 2 — The GATT tree

**Method.** A full GATT dump, with MYBIKE closed and the sensors forgotten on the
iPhone.

**Result.** One vendor service `0xFFF0` carrying the data, notifiable on `0xFFF4`.
Standard Environmental Sensing (`0x181A`) is **absent**, as is any standard pressure
characteristic — so there was never a shortcut through the Bluetooth SIG profiles.
Full tree in `20-protocol.md` §3.2.

## 3 — Passive listen: no handshake

**Method.** Subscribe to everything notifiable and log timestamped
hex, then pressure is changed and the moving bytes are watched.

**Result.** The sensor **streams unprompted at 2 Hz as soon as notifications are
enabled**. No handshake write, no kick, no checksum. A 30-byte message arrives as two
15-byte notifications, and the pressure sits entirely in the first of them, so nothing
has to be reassembled to read it. The decode is `20-protocol.md` §3.3.

The finding that made the numbers trustworthy is §3.4: the pressure float is not an
arbitrary scaling but the FXTH87 sensor chip's own 9-bit code, rescaled. Measured
values land on a 1.671 kPa lattice, which is that code's step. A decode that produces
values off the lattice is wrong.

## 4 — MYBIKE APK static analysis: **not done**

Would have been the highest value per unit effort — UUID literals and the parsing code
without any radio. It became unnecessary once experiments 1–3 produced a working
decode, and it is recorded here only so nobody assumes it was the source. The one piece
of knowledge that does come from outside first-hand measurement is the internal SPI
payload in §1.1, which comes from the community firmware project in `90-references.md`.

## 5 — ANT: confirmed, then dropped

The stock sensor does transmit ANT; a Garmin stick heard it. The one correction worth
recording is that the device type is **26**, not the 48 that the community firmware's
reconstruction of the draft ANT+ TPMS profile suggests.

It went no further. ANT was only ever interesting as a fallback and as a second witness
to check the BLE decode against, and BLE turned out to need neither. The raw log from
that session was not retained, so treat the device type as observed-but-unrecorded.
Tooling and channel parameters have been removed; what is left is `20-protocol.md` §2.

## 6 — RF survey inside the shell: **not done**

Still open, and the one piece of recon that the vehicle itself can answer. AIRSPY range
is about 8 m, a carbon shell is RF-opaque while glass fibre is largely transparent, the
front wheels may sit in wells partitioned from the cockpit, and the sensor **rotates
with the wheel**, so it may null out at certain angular positions. The thing to log is
RSSI and packet loss per wheel from the receiver's intended position, **with the wheels
turning**.

---

## Phase 0 — toolchain verification (2026-10-07)

Not an AIRSPY experiment, but two findings that affect how every later capture is read.

**Environment confirmed working:** `bleak 3.0.2` on `Python 3.14.7`, macOS
CoreBluetooth backend. A live 10-second scan saw 18 devices and 112 advertisements, so
scanning, the detection callback, and `AdvertisementData` field access all work.
(bleak 3.x is a major release; the 0.2x examples found online do not all apply.)

**CoreBluetooth merges the scan response into the advertisement.** Observed
first-hand: three unrelated devices — a router and two "EWD" devices — each produced a
payload that *grew* mid-scan while every byte stayed identical:

    13 48 57 35 31 5a 45 48 34 53 46 36 46 38 32 33 33 00 00 00 00 00 0e ca
    13 48 57 35 31 5a 45 48 34 53 46 36 46 38 32 33 33 00 00 00 00 00 0e ca c5 c5 13 30 ...

A naive "has the payload changed?" test flags all three. In experiment 1 that would
have read as *the sensor broadcasts its pressure* — the wrong conclusion at the point
where it costs the most.

The scanner was therefore changed to report a change only when bytes differ at an
offset both payloads actually have, and to report growth separately as "payload grew".
After the fix, the same scan reported zero changes and three growths.

**Consequence:** a positive advertisement result means *genuine byte changes whose
timestamps line up with when air was bled* — not merely "the payload differed". The
timestamps recorded alongside the capture are what make that difference, which is the
practical reason for ADR-003.
