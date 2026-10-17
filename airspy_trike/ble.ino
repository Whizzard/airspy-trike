// BLE: hearing the sensors, connecting to them, holding the links up.
// Part of the airspy_trike sketch: the Arduino build concatenates every
// .ino in this folder, so the types and globals in airspy_trike.ino are
// visible here without a header.

static void noteSeen(const char* name, const NimBLEAddress& a, int rssi) {
  portENTER_CRITICAL(&seenMux);
  int free = -1;
  for (int i = 0; i < MAX_SEEN; i++) {
    if (seenTab[i].used && strcmp(seenTab[i].name, name) == 0) {
      seenTab[i].addr = a; seenTab[i].rssi = rssi; seenTab[i].t = millis();
      portEXIT_CRITICAL(&seenMux); return;
    }
    if (!seenTab[i].used && free < 0) free = i;
  }
  if (free >= 0) {
    strncpy(seenTab[free].name, name, sizeof(seenTab[free].name) - 1);
    seenTab[free].name[sizeof(seenTab[free].name) - 1] = 0;
    seenTab[free].addr = a; seenTab[free].rssi = rssi;
    seenTab[free].t = millis(); seenTab[free].used = true;
  }
  portEXIT_CRITICAL(&seenMux);
}

// --- notifications ------------------------------------------------------- //
static float readF32(const uint8_t* p) { float f; memcpy(&f, p, 4); return f; }
// A notification from one sensor. Two of these make one 30-byte message, and
// the pressure lives entirely in the first, so nothing has to be reassembled
// to read it. Runs on the BLE task, not in loop().
static void onNotify(int li, uint8_t* d, size_t len) {
  if (len != 15) return;
  conn[li].lastRx = millis();
  if (d[14] == TAG_PRESSURE) {
    float kPa = readF32(d + 8);
    if (kPa > -50 && kPa < 1500) {
      Link& c = conn[li];
      c.ring[c.ringPos] = kPa / 100.0f;
      c.ringPos = (c.ringPos + 1) % AVG_N;
      if (c.ringFill < AVG_N) c.ringFill++;
      float sum = 0;
      for (uint8_t k = 0; k < c.ringFill; k++) sum += c.ring[k];
      c.bar = sum / c.ringFill;        // averages whatever is there yet

      // A sensor that starts reporting again is worth looking at: it woke up,
      // which on this vehicle means a wheel just turned.
      if (!c.haveData) lightUntil = millis() + LIGHT_MS;
      c.haveData = true;

      if (!isnan(c.tempC)) {
        if (isnan(c.baseBar) && c.ringFill >= AVG_N) {
          c.baseBar = c.bar; c.baseTemp = c.tempC;
        } else if (!isnan(c.baseBar)) {
          float norm = (c.bar + P_ATM) * (c.baseTemp + 273.15f)
                                       / (c.tempC + 273.15f) - P_ATM;
          if (norm > c.baseBar + PUMP_BAR) {          // pumped up
            c.baseBar = c.bar; c.baseTemp = c.tempC;
            c.leaking = false;
            lightUntil = millis() + LIGHT_MS;
          } else if (norm < c.baseBar - LEAK_BAR) {   // losing air
            if (!c.leaking) { c.leaking = true; lightUntil = millis() + LIGHT_MS; }
          }
        }
      }
    }
  } else if (d[6] == TAG_TEMP && d[13] == TAG_BATTERY) {
    conn[li].tempC = readF32(d + 0);
    conn[li].batt  = readF32(d + 7);
  }
}

// --- connection state machine -------------------------------------------- //
// No default argument: the Arduino preprocessor emits its own prototype and
// the default would then be declared twice.
static bool findSeen(const String& serial, NimBLEAddress& out, uint32_t maxAge) {
  bool hit = false;
  portENTER_CRITICAL(&seenMux);
  for (int i = 0; i < MAX_SEEN; i++) {
    if (seenTab[i].used && serial == seenTab[i].name &&
        millis() - seenTab[i].t < maxAge) { out = seenTab[i].addr; hit = true; break; }
  }
  portEXIT_CRITICAL(&seenMux);
  return hit;
}
// Let go of a link and put it back in the queue. Never deletes the client:
// doing that mid-procedure once left the controller unable to scan at all.
static void dropLink(int i, const char* why) {
  Serial.printf("[link %d] drop: %s\n", i, why);
  // The client is never deleted. Deleting one while the controller still had a
  // connect procedure outstanding left it stuck initiating for good: after the
  // first timeout every later connect was refused and the scan could not start
  // again, which is exactly what the log showed -- two wheels online, then one
  // timeout, then nothing but "scan on" forever.
  if (conn[i].client) {
    if (conn[i].client->isConnected()) conn[i].client->disconnect();
  }
  conn[i].subscribed = false;
  conn[i].haveData   = false;
  // disconnect() is fire and forget: the handle clears only when the host
  // reports the disconnection. Give it a moment before trying again.
  conn[i].nextTry    = millis() + 1000;
  conn[i].ringFill = conn[i].ringPos = 0;   // never average across a gap
  conn[i].alarmed  = false;
  // The leak reference deliberately survives. A link dropping and coming back
  // is an everyday event mid-ride -- a sensor dozing at a traffic light is
  // enough -- and taking a fresh reference each time would reset the very
  // comparison that makes a slow puncture visible.
}

// A failed attempt, with the backoff that stops a sensor being hammered: half a
// second, then a second, up to thirty. Both ways of failing want exactly this.
static void failLink(int i, const char* why) {
  dropLink(i, why);
  conn[i].backoff = min<uint32_t>((uint32_t)conn[i].backoff * 2, 30000);
  conn[i].nextTry = millis() + conn[i].backoff;
}

// Discovery and subscription, once a connection stands.
static bool finishSubscribe(int i) {
  Link& s = conn[i];
  NimBLERemoteService* svc = s.client->getService(SVC_UUID);
  NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(CHR_UUID) : nullptr;
  if (!chr || !chr->canNotify()) return false;
  if (!chr->subscribe(true, [i](NimBLERemoteCharacteristic*, uint8_t* d, size_t l, bool) {
        onNotify(i, d, l);       // captures the LINK, which never moves
      })) return false;
  s.subscribed = true; s.lastRx = millis(); s.backoff = 500;
  s.subAt = millis(); s.logged = false; s.known = true;
  // Now that the link stands, ask for the slow interval. A request, not a
  // demand: the sensor may refuse it, and the link survives either way.
  s.client->updateConnParams(320, 400, 4, 600);
  s.addr = s.client->getPeerAddress();
  s.haveAddr = true;
  savePositions();
  Serial.printf("[link %d] %s subscribed at %s\n", i, s.serial.c_str(),
                s.addr.toString().c_str());
  return true;
}

// Rebuilt around what a standalone test actually proved. That test scans, stops
// scanning, connects to each sensor in turn with a plain blocking call and holds
// three links for as long as you leave it running -- so three connections are
// not the problem and never were. The difference was here: asynchronous
// connects with the scan being started and stopped underneath them.
//
// Blocking was avoided because it would freeze the display for the length of
// the timeout. The answer is not to make connecting asynchronous but to only
// attempt it when the sensor has just been heard, in which case it answers in
// well under a second and there is nothing to freeze.
static void serviceLink(int i) {
  Link& s = conn[i];
  if (s.serial.isEmpty()) return;

  if (s.subscribed) {
    if (s.client && s.client->isConnected()) {
      // Connected but silent: the sensor is asleep or the link is a zombie.
      if (millis() - s.lastRx > DEAD_MS) dropLink(i, "silent");
      // updateConnParams is a request the sensor may refuse, and refusing it
      // leaves the link at 30-50 ms with no latency: the sensor wakes its radio
      // twenty to thirty times a second to send twice. That is the difference
      // between a coin cell lasting weeks and lasting a day, so report what was
      // actually agreed rather than what was asked for.
      if (!s.logged && millis() - s.subAt > 3000) {
        s.logged = true;
        uint16_t itvl = s.client->getConnInfo().getConnInterval();
        Serial.printf("[link %d] interval %u units = %u ms\n",
                      i, itvl, (unsigned)(itvl * 125 / 100));
      }
      return;
    }
    dropLink(i, "lost");
  }
  if (millis() < s.nextTry) return;

  // Recently heard, or we do not try at all -- that is what keeps the blocking
  // call below short.
  NimBLEAddress target;
  if (!findSeen(s.serial, target, FRESH_MS)) { s.nextTry = millis() + 500; return; }
  s.addr = target; s.haveAddr = true;

  // A handle that has not been torn down yet would make connect() return false
  // with "already connected" and earn a doubled backoff for a failure that
  // never reached the air.
  if (s.client && s.client->isConnected()) { s.nextTry = millis() + 500; return; }

  if (!s.client) {
    s.client = NimBLEDevice::createClient();
    if (!s.client) { s.nextTry = millis() + 2000; return; }   // all three in use
    s.client->setSelfDelete(false, false);
  }
  // No connection parameters here: the test that holds three links sets none.
  // Ten seconds, the value that test uses. Shortening it to protect the display
  // was backwards -- on expiry the library calls ble_gap_conn_cancel(), the
  // same path that leaves the controller stuck initiating, so a short timeout
  // fires that cancel more often, not less.
  s.client->setConnectTimeout(10000);

  Serial.printf("[link %d] connecting to %s\n", i, target.toString().c_str());
  // Keep the attribute table for a peer we have already discovered: a full
  // rediscovery is a burst of transactions the sensor has to serve, every time.
  if (!s.client->connect(target, /*deleteAttributes=*/ !s.known)) {
    failLink(i, "connect failed");
    return;
  }
  if (!finishSubscribe(i)) failLink(i, "no notifiable 0xFFF4");
}

// The scan is the single most expensive thing this device does: a continuous one
// keeps the receiver on permanently. Two savings, neither costing anything:
// it is switched off entirely once every assigned sensor is connected, and
// otherwise it runs at a duty cycle matched to how urgently we are looking.
// window/interval is the radio's own mechanism for that -- the receiver is only
// powered for `window` out of every `interval`.
static void updateScanning() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  bool learning = (mode == M_LEARN || mode == M_PICK);
  bool missing  = false;
  for (int i = 0; i < NPOS; i++)
    if (!conn[i].serial.isEmpty() && !conn[i].subscribed) missing = true;

  bool want = learning || missing;
  static int wasWanted = -1;
  if ((int)want != wasWanted) {
    wasWanted = want;
    Serial.printf("scan %s\n", want ? "wanted" : "off: all links up");
  }

  // Each start opens a fresh session and clears the duplicate cache. There is
  // no stopping here for the sake of a connect: NimBLEClient::connect() already
  // handles BLE_HS_EBUSY by stopping the scan itself and retrying. Doing it
  // here as well is what had the two of us restarting the scan in the same
  // tick, forever.
  if (want) {
    if (!scan->isScanning()) {
      // Active scanning asks every advertiser for its scan response, which the
      // sensor has to transmit. Only learning needs the names; after that an
      // address is enough, and a sensor whose address we lack is a learning
      // case anyway.
      bool needNames = learning;
      for (int i = 0; i < NPOS; i++)
        if (!conn[i].serial.isEmpty() && !conn[i].haveAddr) needNames = true;
      scan->setActiveScan(needNames);
      scan->start(SCAN_SESSION_MS, false, true);
    }
  } else if (scan->isScanning()) {
    scan->stop();
  }
}
