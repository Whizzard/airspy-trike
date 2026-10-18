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
// Once the sensors are learned, something goes wrong every minute or so on a
// wheel picked at random: a slow puncture, a fall through the alarm threshold,
// or a cell running flat. Each runs its course, sits at the bottom for a while
// and is then put right -- pumped up, or replaced. A demo that only ever shows
// the quiet state proves nothing about the half of this device that matters.
//
// Entered by holding A while the device starts. Nothing touches NimBLE in this
// mode: the radio is never initialised, so there is nothing to interfere with.

static String        fakeSerial[NPOS];
static NimBLEAddress fakeAddr[NPOS];

static const float FAKE_BAR0[NPOS]  = { 6.20f, 5.90f, 6.50f };
static const float FAKE_BATT0[NPOS] = { 92.0f, 78.0f, 24.0f };

static Ramp fakeP[NPOS];              // pressure offset, bar
static Ramp fakeB[NPOS];              // battery level, percent
static uint32_t fakeNextEv = 0;

static bool rampBusy(const Ramp& r) { return r.rate != 0.0f || r.hold != 0; }

// Advance one ramp by half a second. Returns true on the tick where the pause
// ends and it starts back, so the caller can say so.
static bool stepRamp(Ramp& r, uint32_t now) {
  if (r.rate < 0.0f) {
    r.v += r.rate * 0.5f;
    if (r.v <= r.tgt) {
      r.v = r.tgt; r.rate = 0.0f;
      r.hold = now + (uint32_t)random(20000, 40000);
    }
  } else if (r.rate > 0.0f) {
    r.v += r.rate * 0.5f;
    if (r.v >= r.home) { r.v = r.home; r.rate = 0.0f; }
  } else if (r.hold && (int32_t)(now - r.hold) >= 0) {
    r.hold = 0;
    if (r.back > 0.0f) r.rate = r.back;
    else               r.v = r.home;          // a new cell, not a slow refill
    return true;
  }
  return false;
}

// Pick a wheel and start something on it. The three kinds are reached
// differently, which is the point of having all three: a slow puncture is a
// fall of more than LEAK_BAR below the reference at any speed, the alarm needs
// the pressure to cross the threshold outright, and a flat cell is neither.
static void startFakeEvent() {
  int i = (int)random(NPOS);
  switch ((int)random(3)) {
    case 0:
      if (rampBusy(fakeP[i])) return;
      // 0.15 bar a minute, down 0.45: the warning appears after about two
      // minutes -- long enough to watch happen, short enough to sit out.
      fakeP[i].tgt = -0.45f; fakeP[i].rate = -0.0025f;
      fakeP[i].back = 0.040f; fakeP[i].home = 0.0f;
      Serial.printf("demo: %s springs a slow leak\n", POS_LABEL[i]);
      break;
    case 1: {
      if (rampBusy(fakeP[i])) return;
      // Down past the threshold, so the alarm raises for real. Read off the
      // threshold actually in force rather than a constant, so this still
      // works after the thresholds have been set by hand.
      fakeP[i].tgt = thrFor(i) - FAKE_BAR0[i] - 0.25f;
      fakeP[i].rate = -0.010f;                      // 0.6 bar a minute
      fakeP[i].back = 0.040f; fakeP[i].home = 0.0f; // somebody with a floor pump
      Serial.printf("demo: %s is losing air\n", POS_LABEL[i]);
      break;
    }
    default:
      if (rampBusy(fakeB[i])) return;
      fakeB[i].tgt = 6.0f; fakeB[i].rate = -1.2f;   // flat in about a minute
      fakeB[i].back = 0.0f; fakeB[i].home = 100.0f; // and then a new cell
      Serial.printf("demo: %s battery going flat\n", POS_LABEL[i]);
      break;
  }
}

static void fakeEvents(uint32_t now) {
  for (int i = 0; i < NPOS; i++) {
    if (stepRamp(fakeP[i], now))
      Serial.printf("demo: %s gets pumped back up\n", POS_LABEL[i]);
    if (stepRamp(fakeB[i], now))
      Serial.printf("demo: %s gets a new cell\n", POS_LABEL[i]);
    fakeB[i].v -= 0.002f;                           // and the slow drain of use
  }
  if ((int32_t)(now - fakeNextEv) >= 0) {
    fakeNextEv = now + (uint32_t)random(45000, 90000);
    startFakeEvent();
  }
}

static float fakeBar(int i, float t) {
  float v = FAKE_BAR0[i] + fakeP[i].v;
  v += 0.04f * sinf(t / 11.0f + i);                 // the tyre breathing
  v += 0.025f * sinf(t * 7.3f + i * 2.1f);          // measurement noise
  // The sensor reports a 9-bit code scaled by 16.71 hPa, so its values can only
  // ever sit on that lattice. Rounding here keeps the display honest about how
  // coarse the real steps are.
  float kPa = v * 100.0f;
  return roundf(kPa / 1.671f) * 1.671f / 100.0f;
}

// Start with nothing assigned, so the whole process is exercised and not just
// the readings: the learn window opens, the sensors turn up one after another,
// the counter climbs, and the assignment follows from what was heard.
static void fakeBegin() {
  randomSeed(esp_random());
  fakeNextEv = millis() + 45000;
  // Its own thresholds, starting at the default. Changing them in here never
  // reaches the flash -- see savePositions() -- so the vehicle's own settings
  // are still there after a reset.
  thrFront = thrRear = 5.0f;
  for (int i = 0; i < NPOS; i++) {
    fakeP[i] = Ramp();
    fakeB[i] = Ramp();
    fakeB[i].v = FAKE_BATT0[i];
    fakeB[i].home = 100.0f;
    fakeSerial[i] = String("AIRSPY-") + String((int)random(10000, 99999));
    fakeAddr[i]   = NimBLEAddress((uint64_t)0xf82600000000ULL + random(1, 0xFFFFFF), 1);
    Serial.printf("demo: %s at %s\n", fakeSerial[i].c_str(),
                  fakeAddr[i].toString().c_str());
  }
  forgetAssignment();
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
  if (mode != M_LEARN) fakeEvents(now);             // nothing goes wrong mid-learn
  float t = now / 1000.0f;

  for (int i = 0; i < NPOS; i++) {
    if (conn[i].serial.isEmpty()) continue;         // not assigned yet
    conn[i].subscribed = true;

    float kPa  = fakeBar(i, t) * 100.0f;
    float temp = 18.5f + 1.5f * sinf(t / 23.0f + i) + 0.1f * i;
    float batt = constrain(fakeB[i].v, 1.0f, 100.0f);

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
