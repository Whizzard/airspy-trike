# SKS AIRSPY protocol specification

> **Status: BLE decoded 2026-10-13**, see §3. The remaining gap is the absolute
> pressure offset (§3.4). Every
> claim carries a label:
> **[CONFIRMED]** observed first-hand or stated in vendor documentation ·
> **[INFERRED]** reasoned from other evidence, not yet observed ·
> **[UNKNOWN]** no information.
>
> Keep the labels honest. Inference mistaken for fact is how a week gets lost.

## 1. Device architecture **[CONFIRMED]**

Two chips, which is the key to the whole protocol:

- **NXP/Freescale FXTH87** — automotive TPMS sensor-on-chip: pressure, temperature,
  accelerometer, voltage. Its own 315/433 MHz radio is **unused** in the AIRSPY.
- **Nordic nRF52832** — provides all 2.4 GHz radio (BLE *and* ANT). Reads the FXTH87
  over a one-way SPI link.

Source: `bitmeal/sks_airspy_ant_community_fw` `doc/HARDWARE_PROTO.md`.

**Why this matters:** the nRF52832 can only ever transmit what the FXTH87 hands it over
SPI. That internal payload has been reverse-engineered, so **the set of fields any
radio protocol can carry is already known** — we are searching for a known needle, not
guessing blind.

### 1.1 Internal SPI payload **[CONFIRMED]** — the upstream source of truth

SPI clock 10.37 kHz, nRF is master, FXTH87 raises `/INT` to request a transfer.
Six bytes; the whole buffer is shifted left by one bit.

| Bits | Field | Conversion |
|---|---|---|
| `[0,17)` | pressure code | `P_hPa = code × 16.75 − 248` |
| `[17,25)` | temperature | `°C = raw − 55` |
| `[25,33)` | voltage | `V = (raw + 122) / 100` |
| `35` | undervoltage flag | mask `0x20` |
| `[40,48)` | checksum | XOR of first 5 bytes; ignore lowest bit after shift |

Constants: `SENSOR_CLAMP_PRESS_LOW_HPA 75`, slope `16.75`, offset `−248`,
temp `−55`, volt `+122`.

**Pressure field width — [INFERRED], and a strong inference.** The source comment is
ambiguous (`[0,17)` vs. `b[9:17]`), but a **9-bit** code is almost certainly right:

    max code 511 × 16.75 − 248 = 8311 hPa = 8.31 bar

which is exactly SKS's published maximum of **8.3 bar / 120 psi**. The advertised spec
falls straight out of the encoding, which is unlikely to be coincidence. Resolution is
16.75 hPa ≈ **0.017 bar** ≈ 0.24 psi. Confirmed empirically; see 3.4.

## 2. ANT **[CONFIRMED to exist, not pursued]**

The stock sensor transmits ANT as well, and SKS's own Garmin integration uses it
exclusively: the SKS AIRSPY Connect IQ data field declares exactly one permission,
`Ant`. `BluetoothLowEnergy` is absent from its manifest.

**[CONFIRMED]** Pairing on Garmin is by typing the **laser-engraved serial** into the
data field's settings, so the serial is the 16-bit ANT device number.

**[CONFIRMED]** It does *not* interoperate with generic ANT+ TPMS displays (Wahoo and
Karoo do not pair), so it does not implement the draft ANT+ TPMS profile faithfully.

**[OBSERVED, unrecorded]** A Garmin stick heard a stock sensor on **device type 26**,
not the 48 that the community firmware's reconstruction of the draft profile suggests.
The raw log from that session was not kept.

This path was not taken any further. ANT was only ever interesting as a fallback and as
an independent second witness against the BLE decode, and BLE needed neither. The
tooling and the channel-parameter hypotheses have been removed; section 3 is the
protocol this project uses.

## 3. BLE — **[LARGELY CONFIRMED]** as of 2026-10-13

Measured first-hand against sensor `AIRSPY-14203` on 2026-10-13. Advertisements
were read with a LilyGO T-Display
(ESP32) rather than macOS, because CoreBluetooth hides the BD_ADDR and normalises
parts of the payload; the ESP32 reports the raw bytes and the real MAC.

| Question | Answer |
|---|---|
| Advertised local name | **[CONFIRMED]** `AIRSPY-<serial>`, e.g. `AIRSPY-14203` |
| Manufacturer data, company ID | **[CONFIRMED]** `0x5253` (ASCII `"SR"`, squatted), then `"TMPS-1111-V1.0"`, then the MAC |
| Pressure in the advertisement? | **[CONFIRMED] No.** Constant across 41 advertisements while pressure was actively bled |
| Service / characteristic UUIDs | **[CONFIRMED]** see 3.2 |
| Environmental Sensing (`0x181A`)? | **[CONFIRMED] Absent.** No standard pressure characteristic |
| Handshake needed? | **[CONFIRMED] No.** The sensor streams unprompted at 2 Hz on connect |
| Pairing / bonding | **[UNKNOWN]** — connected without pairing; whether MYBIKE bonds is untested |
| Payload field layout | **[CONFIRMED]** see 3.3 |

### 3.1 Advertisement **[CONFIRMED]**

Address `f8:26:ac:39:d5:38`, **random static** (type 1), stable across the session.

| AD type | Value |
|---|---|
| `0x01` Flags | `0x06` — LE General Discoverable, BR/EDR not supported |
| `0x03` Complete 16-bit Service UUIDs | `0xFFF0` |
| `0x09` Complete Local Name | `AIRSPY-14203` |
| `0xFF` Manufacturer Specific Data | `53 52` + `"TMPS-1111-V1.0"` + the 6-byte MAC |
| `0x19` Appearance | `0x0000` |

**The serial is in the local name.** This settles the §3 inference and solves
per-wheel identity on macOS/iOS without pairing: the name is visible while
scanning, where the MAC is not.

`0x5253` is not an assigned Bluetooth SIG company identifier and `0xFFF0` is the
usual placeholder range — together with `"TMPS-1111-V1.0"` this points at a
bought-in BLE module running near-default configuration, not an SKS design.

### 3.2 GATT tree **[CONFIRMED]**

Negotiated MTU 23. Four services, six characteristics:

| Service | Characteristic | Properties | Value |
|---|---|---|---|
| `0x180F` Battery | `0x2A19` Battery Level | notify, read | `0x64` = 100 % |
| **`0xFFF0`** | `0xFFF1` | write, write-without-response | — |
| | **`0xFFF4`** | **notify** | **the data stream** |
| | `0xFFF3` | notify, write-without-response | — |
| `8e400001-f315-4f60-9fb8-838830daea50` | same UUID | notify, write | — |
| `0x180A` Device Information | `0x2A23` System ID | read | `51 61 45 23 01 34 22 01` |

`0x2A23` contains neither the MAC nor the serial — it is not an identity key.
Nordic Secure DFU (`0xFE59`) was **not** present in normal operation.

### 3.3 The data stream on `0xFFF4` **[CONFIRMED]**

One logical 30-byte message arrives as **two 15-byte notifications** with the same
timestamp, at 2 Hz, unprompted. Layout:

    d5 c3 00 19 00 00 03 00 | f32 00 00 2E | f32 00 00 22 | f32 00 00 1E | 0D
    +---- header, constant --+  pressure      temperature    battery       end

Each record is a little-endian IEEE-754 `float32`, two zero bytes, and a one-byte
field tag. The split falls inside the second record, so the **pressure float is
entirely within the first notification** (offsets 8..11) — a receiver that only
wants pressure never needs to reassemble.

| Tag | Quantity | Unit | Observed |
|---|---|---|---|
| `0x2E` | pressure | **kPa** | 20.678 … 82.505 |
| `0x22` | temperature | **°C** | 18.01 … 18.99 |
| `0x1E` | battery | **%** | 100.0 |

**How the units were established.** Over a 91 s capture the tyre was bled and then
re-inflated. The `0x2E` field tracks that sequence exactly: 40.7 → 20.7 → plateau
→ steady rise to 82.5. Read as kPa this is 0.21–0.83 bar, consistent with the
inner tube under test; hPa would be 10x low and psi 7x high.

`0x1E` is battery percent, confirmed against the SIG Battery Level characteristic
`0x2A19` at two widely separated points on 2026-10-13: both read 100 in the
morning, and `0x1E` = 41 against `0x2A19` = 38 the same evening. Two points apart
rather than one shared value, so the scale is established and not merely a
coincidence at full charge.

> **Holding a connection drains the sensor fast.** That 100 → 38 happened in one
> day of being held in a BLE connection on NimBLE's default parameters (30–50 ms
> interval, zero slave latency): the peripheral must wake its radio at every
> connection event, so it woke 20–33 times a second to deliver two messages. A
> receiver should ask for an interval near the sensor's own 2 Hz cadence, and
> should disconnect when the vehicle is parked — the sensor sleeps ~10 minutes
> after motion stops, but only if it is left alone to do so.
>
> Note also that MYBIKE's battery indicator has only four steps and still showed
> a healthy level at 38 %, so it is not a usable reference for this.

### 3.4 The pressure float is the FXTH87 code, rescaled **[CONFIRMED]**

The 20 distinct pressure values in the capture lie **exactly** on an integer
lattice:

    P_kPa = 1.6710 x code + c

All 20 fit to within rounding; the span is 61.827 kPa over exactly 37 steps. A
quantisation of 16.710 hPa is **0.24 % from the 16.75 hPa slope documented in
§1.1** for the FXTH87's internal SPI payload.

So the BLE value is not an independent measurement: it is the sensor's 9-bit
pressure code put through a linear conversion. This is the first empirical
confirmation of §1.1, which until now rested entirely on third-party source code.

**[UNKNOWN]** the absolute offset. Pinning it needs steady-state readings against
a floor-pump gauge; against MYBIKE alone the fit was 0.72 bar vs. 0.66 bar
displayed, within the noise of a freshly inflated tube (per-sample standard
deviation 4.7 kPa). The documented `-248 hPa` offset and the measured slope
cannot both hold with integer codes, so at least one constant differs from the
community firmware's.

### 3.5 What little is known around the edges **[CONFIRMED]**

- **BLE is connection-oriented and exclusive.** SKS's troubleshooting KB tells users to
  check whether the sensors are "already connected to another device nearby". One
  central at a time.
- There is a **BLE scan** step: on Android, MYBIKE needs location permission on first
  connection — the classic Android BLE-scan requirement.
- In a failed-firmware-update state the sensor advertises as **`TPMS_DFU`** — a renamed
  Nordic DFU bootloader advertisement. Stock DFU payloads are **signed with a key only
  SKS holds**.
- The sensor sleeps roughly 10 minutes after motion stops, and must be shaken or spun
  awake before it advertises at all.

### 3.6 What a receiver has to do

Nothing exotic: scan for a name matching `AIRSPY-<serial>`, connect, enable
notifications on `0xFFF4`, and read a `float32` at offset 8 of every notification
whose byte 14 is `0x2E`. No pairing, no handshake write, no checksum to verify. The
name needs an *active* scan, because 49 bytes of advertisement data do not fit the
31-byte limit and the name lives in the scan response.

The 2 Hz stream sets the achievable refresh rate. Exclusivity means no *other* central
may hold a given sensor -- it does **not** force round-robin polling, because one
central may hold several peripherals at once. An ESP32 running NimBLE holds three by
default, which is what `airspy_trike/` does.

One cost to know about: holding a sensor in a connection keeps its radio busy. One
sensor fell from 100 % to 38 % battery in a single day of being held, so a receiver
should ask for the slowest connection interval the sensor will accept -- 400--500 ms
instead of the usual 30--50 -- and should be unplugged when the vehicle is parked.
