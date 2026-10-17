# Project: three-wheel tyre-pressure monitor for a tadpole velomobile

## Goal

A live display of tyre pressure for all three wheels of a tadpole velomobile — two
front, one rear — reading three SKS AIRSPY sensors, mounted on the handlebar and
powered from the vehicle's own battery.

## Why a velomobile changes the calculus

- The tyres are largely **enclosed by the shell**. A soft tyre is not visible while
  riding, and often not while loading the vehicle either.
- Velomobile speeds and loads make a slow leak more consequential than on an upright
  bike: handling of a tadpole degrades asymmetrically if one *front* tyre goes soft,
  which is the hardest case to notice and the one that matters most.
- Three wheels is three things to forget. The failure mode this project targets is not
  a blowout (you feel that) but a **slow leak discovered too late**.
- Front and rear carry very different loads, so they run different target pressures.
  The monitor must hold a per-wheel target, not one global number.

## Requirements

### Must have
- Live pressure for all three wheels, each identified by **wheel position**, not by
  sensor serial — a glance must map to a wheel without reading labels.
- Per-wheel target pressure and tolerance band.
- Alarm on deviation from target **and on loss of a sensor**. A silent sensor is
  itself information and must never read as "fine".
- **Reading age visible per wheel.** A sensor sleeps about ten minutes after the wheel
  stops, so a value can be stale. The display must not pretend otherwise: a reading
  that has gone quiet shows a dash, not its last number.
- Readable in direct sunlight at speed, with the backlight off.

### Should have
- Costs the sensors' own coin cells as little as possible. Holding one in a BLE
  connection is expensive for it — one measured fall from 100 % to 38 % in a day — so
  the connection interval is negotiated as slow as the sensor will accept.
- Ride log to flash for post-ride leak analysis.

### Won't have
- Reflashing the sensors. See ADR-002.
- Any claim to replace a pre-ride pressure check.
- Phone dependency while riding.

## Hardware on hand

| Item | Role |
|---|---|
| 3 × SKS AIRSPY sensors | the subject; stock firmware, Presta/SV |
| LilyGO T-Display (ESP32) | the monitor; a T-Display-S3 is supported by the same sketch |
| Adafruit nRF52840 board | BLE sniffer or central, if a capture is ever needed below the host stack |
| iPhone | runs SKS MYBIKE as the reference implementation and ground truth |
| macOS workstation | bleak host, Wireshark, APK analysis |

### Hardware gaps, and what they cost us

- **No Android phone.** This removes `btsnoop_hci.log`, normally the single
  highest-value technique for understanding a vendor app — it records the phone's own
  HCI interface, so everything is captured already decrypted, above link-layer crypto.
  It cost nothing in the end: the sensor needs no handshake, so there was no exchange
  with MYBIKE left to recover. A used Android phone (~€30) remains the escape hatch if
  the configuration writes ever need to be understood.

## Sensor facts relevant to every experiment

All CONFIRMED from SKS documentation — see `docs/90-references.md`.

- **Wakes on motion**, sleeps after **~10 minutes** without motion. Every experiment
  begins by shaking the sensor or spinning the wheel. A sensor that "stopped working"
  mid-session has most likely just gone to sleep.
- Pressure range **0.0–8.3 bar / 120 psi**; stated accuracy ±1 % of reading.
- **CR2032**, user-replaceable, ~500 h operating time.
- Radio range **~8 m**.
- MYBIKE warns at **15 %** (yellow) and **25 %** (red) deviation from target — a
  sensible default for our own alarm thresholds.
- **A sensor connected to one device is unavailable to another** (SKS's own
  troubleshooting advice). This exclusivity drives the whole architecture question.
