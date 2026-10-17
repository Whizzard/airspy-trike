// A demo mode with three invented sensors, for working on the device when the
// real ones are on the vehicle and the vehicle is somewhere else.
//
// The readings are not injected into the display. They are assembled into the
// same 15-byte notifications an AIRSPY sends and handed to onNotify, so
// everything downstream -- the pairing of the two halves, the averaging, the
// alarm hysteresis, the leak reference and its temperature compensation -- is
// exercised exactly as it would be in the garage. Pressures even land on the
// 1.671 kPa lattice the real sensor quantises to, because code that only ever
// sees smooth numbers has not met the real thing.
//
// Entered by holding A while the device starts. Nothing touches NimBLE in this
// mode: the radio is never initialised, so there is nothing to interfere with.

static String        fakeSerial[NPOS];
static NimBLEAddress fakeAddr[NPOS];

// Start with nothing assigned, so the whole process is exercised and not just
// the readings: the learn window opens, the sensors turn up one after another,
// the counter climbs, and the assignment follows from what was heard.
static void fakeBegin() {
  randomSeed(esp_random());
  for (int i = 0; i < NPOS; i++) {
    fakeSerial[i] = String("AIRSPY-") + String((int)random(10000, 99999));
    fakeAddr[i]   = NimBLEAddress((uint64_t)0xf82600000000ULL + random(1, 0xFFFFFF), 1);
    Serial.printf("demo: %s at %s\n", fakeSerial[i].c_str(),
                  fakeAddr[i].toString().c_str());
  }
  forgetAssignment();
}

// Wheel 2 is slowly going down -- about 0.15 bar a minute, so the slow-puncture
// state appears after roughly two, which is long enough to watch it happen and
// short enough to sit through.
static float fakeBar(int i, float t) {
  static const float start[NPOS] = { 6.20f, 5.90f, 6.50f };
  float v = start[i];
  v += 0.04f * sinf(t / 11.0f + i);                 // the tyre breathing
  if (i == HR) v -= 0.0025f * t;                    // and one of them leaking
  v += 0.025f * sinf(t * 7.3f + i * 2.1f);          // measurement noise
  // The sensor reports a 9-bit code scaled by 16.71 hPa, so its values can only
  // ever sit on that lattice. Rounding here keeps the display honest about how
  // coarse the real steps are.
  float kPa = v * 100.0f;
  return roundf(kPa / 1.671f) * 1.671f / 100.0f;
}

static void fakeTick() {
  uint32_t now = millis();

  // Put them in the sightings table the way the scanner would. While learning
  // they arrive staggered, a second and a half apart, so the counter visibly
  // climbs from one to three instead of jumping.
  static uint32_t lastSeen = 0;
  if (now - lastSeen > 400) {
    lastSeen = now;
    int n = NPOS;
    if (mode == M_LEARN) {
      uint32_t since = now - (learnUntil - LEARN_MS);
      n = (int)(since / 1500) + 1;
      if (n > NPOS) n = NPOS;
    }
    for (int i = 0; i < n; i++)
      noteSeen(fakeSerial[i].c_str(), fakeAddr[i], -52 - 4 * i);
  }

  static uint32_t lastData = 0;
  if (now - lastData < 500) return;                 // the real thing sends at 2 Hz
  lastData = now;
  float t = now / 1000.0f;

  for (int i = 0; i < NPOS; i++) {
    if (conn[i].serial.isEmpty()) continue;         // not assigned yet
    conn[i].subscribed = true;

    float kPa  = fakeBar(i, t) * 100.0f;
    float temp = 18.5f + 1.5f * sinf(t / 23.0f + i) + 0.1f * i;
    float batt = constrain(100.0f - t / 20.0f - i * 34.0f, 3.0f, 100.0f);

    uint8_t a[15] = { 0xd5, 0xc3, 0x00, 0x19, 0x00, 0x00, 0x03, 0x00 };
    memcpy(a + 8, &kPa, 4);
    a[12] = a[13] = 0x00; a[14] = TAG_PRESSURE;

    uint8_t b[15];
    memcpy(b + 0, &temp, 4);
    b[4] = b[5] = 0x00; b[6] = TAG_TEMP;
    memcpy(b + 7, &batt, 4);
    b[11] = b[12] = 0x00; b[13] = TAG_BATTERY; b[14] = 0x0d;

    onNotify(i, a, 15);
    onNotify(i, b, 15);
  }
}
