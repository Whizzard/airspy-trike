# References

Gathered 2026-10-07. Mark anything that goes stale rather than deleting it.

## The one existing reverse-engineering project

Reverse-engineered the **internal SPI bus**, not the radio protocol. Source of all our
field-encoding knowledge.

- Repo — https://github.com/bitmeal/sks_airspy_ant_community_fw
- `doc/HARDWARE_PROTO.md` (chips, GPIO, SPI payload, conversion constants) —
  https://github.com/bitmeal/sks_airspy_ant_community_fw/blob/master/doc/HARDWARE_PROTO.md
- `doc/ANT_TPMS_profile.md` (reconstructed draft ANT+ TPMS profile, data pages) —
  https://github.com/bitmeal/sks_airspy_ant_community_fw/blob/master/doc/ANT_TPMS_profile.md
- `CHANGELOG.md` — https://github.com/bitmeal/sks_airspy_ant_community_fw/blob/master/CHANGELOG.md
- Issues (incl. board revisions `TPMS-10-2`, AIRSPY TL compatibility #16) —
  https://github.com/bitmeal/sks_airspy_ant_community_fw/issues
- Fork by alistair23 — https://github.com/alistair23/sks_airspy_ant_community_fw
- Hackaday coverage, Feb 2025 — **read the comments**, one reports decoding a *stock*
  sensor over ANT with a Pi + ANT stick —
  https://hackaday.com/2025/02/25/reverse-engineering-sks-airspy-tire-pressure-sensors-for-custom-firmware/

## SKS official

- AIRSPY ¬ GARMIN / MYBIKE operating instructions, plain PDF (the flipbooks are
  JS-rendered; this mirror extracts cleanly) — contains the `TPMS_DFU` evidence and the
  "disconnect before updating" statement —
  https://www.cykloport.cz/user/related_files/ab_ga_airspy_my_bike_app_en_21_(2).pdf
- Support KB, AIRSPY section (22 articles) —
  https://support.sks-germany.com/hc/de/sections/20589977164689-AIRSPY
  - Unauthenticated JSON API for the whole section:
    `https://support.sks-germany.com/api/v2/help_center/de/sections/20589977164689/articles.json`
  - Wake on motion / 10-min sleep — https://support.sks-germany.com/hc/de/articles/20686608472209
  - "already connected to another device nearby" (exclusivity) — https://support.sks-germany.com/hc/de/articles/22107818398737
  - Not shown in app / Android location permission — https://support.sks-germany.com/hc/de/articles/20687118884753
  - Data transmission — https://support.sks-germany.com/hc/de/articles/20686690801553
  - Range ~8 m — https://support.sks-germany.com/hc/de/articles/20686687580049
  - Transmits ANT+ — https://support.sks-germany.com/hc/de/articles/20687218618001
  - Pressure range — https://support.sks-germany.com/hc/de/articles/20687033828113
  - Battery indicator — https://support.sks-germany.com/hc/de/articles/20687140789649
  - Accuracy / 15 % and 25 % alarm thresholds — https://support.sks-germany.com/hc/de/articles/20687025528081
- Manuals (flipbooks) — https://flip.sks-germany.com/books/uqks/ ·
  AIRSPY×GARMIN https://flip.sks-germany.com/books/yuml/ · safety https://flip.sks-germany.com/books/wobr/
- AIRSPY SET product page — https://www.sks-germany.com/en/Products/Multi-Tools/AIRSPY-SET.htm?a=article&ProdNr=148&p=1006
- AIRSPY TL SET (tubeless) — https://www.sks-germany.com/en/Products/Multi-Tools/AIRSPY-TL-SET.htm?a=article&ProdNr=149&p=1006
- **SKS AIRSPY Connect IQ data field** — the manifest declares only the `Ant`
  permission, which is the strongest evidence that the Garmin path is ANT-only —
  https://apps.garmin.com/en-US/apps/f89f3680-5a47-48d0-baff-3c25c99f5d07

## Not yet retrieved — worth a browser session

- **FCC ID `2ASEG-1161A`** (SKS metaplast Scheffer-Klute GmbH). The RF test report
  usually states the operating mode (advertising vs. connected) and intervals; internal
  photos exhibit — https://fcc.report/FCC-ID/2ASEG-1161A/4672637
  (fcc.report / fccid.io returned 403 to automated fetching; open in a normal browser.)
- Hammerhead community threads, Cloudflare-blocked to automated fetching. The second
  one may contain exactly the kind of log we want:
  "ANT+/BLE Tyre pressure with SKS Airspy not working" (post 6507418507931) ·
  "I'm looking for someone to create an adb log of TPMS Sensor" (post 47130418386587)

## Confirmed negative results — do not re-search these

No AIRSPY support exists in any of these as of 2026-10-07. They cover *generic car*
valve-cap TPMS beacons only, which are a different design.

- Theengs Decoder device list (137 devices; only generic `TPMS`/`TPMSBR`) —
  https://decoder.theengs.io/devices/devices.html
- Home Assistant / ESPHome threads (generic car TPMS, not AIRSPY) —
  https://community.home-assistant.io/t/esphome-and-tire-pressure-sensors-tpms/125080 ·
  https://community.home-assistant.io/t/ble-tire-pressure-monitor/509927
- Gadgetbridge: no AIRSPY support.
- **Bluetooth SIG company identifiers: no entry for SKS, metaplast or Scheffer.** Any
  manufacturer-specific data must sit under Nordic's `0x0059` or a squatted value —
  https://bitbucket.org/bluetooth-SIG/public/raw/main/assigned_numbers/company_identifiers/company_identifiers.yaml
- No published BLE UUID, advertisement layout, pcap, or GATT dump for stock firmware
  anywhere — forums (incl. mtb-news, velomobilforum, rennrad-news), blogs, repos.
- No Python/bleak/ESP32 client exists publicly.

## Tooling

### BLE sniffing on the Adafruit board
- **Adafruit: BLE Sniffer with nRF52840** — the drag-and-drop UF2 route —
  https://learn.adafruit.com/ble-sniffer-with-nrf52840/overview
- Adafruit Bluefruit LE Sniffer (the older nRF51822 product; sniffer v3.1.0 max,
  1 Mbps PHY only, needs J-Link) —
  https://learn.adafruit.com/introducing-the-adafruit-bluefruit-le-sniffer?view=all
- Nordic nRF Sniffer for Bluetooth LE —
  https://www.nordicsemi.com/Products/Development-tools/nRF-Sniffer-for-Bluetooth-LE
- Nordic Academy: sniffing BLE packets, and the follow-and-decrypt exercise (the source
  for the one-connection-at-a-time and CONNECT_IND limitations) —
  https://academy.nordicsemi.com/courses/bluetooth-low-energy-fundamentals/lessons/lesson-6-bluetooth-le-sniffer/topic/sniffing-bluetooth-le-packets/ ·
  https://academy.nordicsemi.com/courses/bluetooth-low-energy-fundamentals/lessons/lesson-6-bluetooth-le-sniffer/topic/blefund-lesson-6-exercise-3/
- extcap not appearing on recent macOS (known friction) —
  https://ask.wireshark.org/question/34968/nrf-sniffer-411-extcap-not-showing-in-interfaces-list-on-macos-sonoma-145/
- Headless fallback: `nrfutil ble-sniffer` + `tshark` —
  https://www.adam-thomas.co.uk/blog/nrfutil-bluetooth-sniffer-with-tshark/
- `bluekitchen/raccoon` — multi-dongle sniffer, one per advertising channel, answers the
  "misses advertisements" limitation — https://github.com/bluekitchen/raccoon

### Host-side
- Adafruit `central_scan_advanced.ino` —
  https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/libraries/Bluefruit52Lib/examples/Central/central_scan_advanced/central_scan_advanced.ino
- CircuitPython `_bleio` — `discover_remote_services()` enumerates an unknown GATT tree
  generically — https://docs.circuitpython.org/en/latest/shared-bindings/_bleio/index.html
- Gadgetbridge's guide to inspecting Bluetooth packets (the btsnoop procedure, for the
  Android escape hatch) — https://gadgetbridge.org/internals/development/bluetooth/

## ESP32

- NimBLE multi-connection, default 3 and up to 9 — this is what makes holding all
  three sensors at once possible —
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/ble/ble-multiconnection-guide.html
- NimBLE-Arduino — https://github.com/h2zero/NimBLE-Arduino
- Arduino_GFX, which drives both the SPI panel of the T-Display and the i8080
  parallel panel of the S3 — https://github.com/moononournation/Arduino_GFX

## Context / reviews (sensor behaviour, battery, wake)

- gpsradler test (DE) — https://gpsradler.de/praxistest/sks-airspy-test/
- NSMB review ("a short jiggle or spin of the wheels awakens it") — https://nsmb.com/articles/sks-airspy-review/
- road.cc review — https://road.cc/content/review/sks-airspy-sv-297525
- mtb-news thread — https://www.mtb-news.de/forum/t/sks-airspy-erfahrungen.914751/
- OpenTPMS, an unrelated open-source bicycle TPMS — https://github.com/TH3-G3NTL3M3N/OpenTPMS
