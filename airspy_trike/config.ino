// What survives a power cut, and which sensor belongs to which wheel.
// Part of the airspy_trike sketch: the Arduino build concatenates every
// .ino in this folder, so the types and globals in airspy_trike.ino are
// visible here without a header.

// --- persistence --------------------------------------------------------- //
// Stored per wheel, not per link: the mapping is the thing worth keeping.
// Serial and address are both stored per wheel, so a rotation carries them both.
static void savePositions() {
  prefs.begin("airspy", false);
  for (int p = 0; p < NPOS; p++) {
    Link& l = at(p);
    prefs.putString(POS_KEY[p], l.serial);
    prefs.putString(ADDR_KEY[p], l.haveAddr
        ? String(l.addr.getType()) + " " + l.addr.toString().c_str()
        : String(""));
  }
  prefs.end();
}

static void saveThresholds() {
  prefs.begin("airspy", false);
  prefs.putFloat("thrF", thrFront); prefs.putFloat("thrR", thrRear);
  prefs.end();
}
// Read back at startup. The leak reference is deliberately not among it:
// that lasts one ride, and losing air between rides is normal.
static void loadConfig() {
  prefs.begin("airspy", true);
  thrFront = prefs.getFloat("thrF", 5.0f);
  thrRear  = prefs.getFloat("thrR", 5.0f);
  for (int p = 0; p < NPOS; p++) {
    conn[p].serial = prefs.getString(POS_KEY[p], "");
    posLink[p] = p;
    String a = prefs.getString(ADDR_KEY[p], "");
    int sp = a.indexOf(' ');
    if (sp > 0) {
      conn[p].addr = NimBLEAddress(std::string(a.substring(sp + 1).c_str()),
                                   (uint8_t)a.substring(0, sp).toInt());
      conn[p].haveAddr = true;
    }
  }
  prefs.end();
  Serial.printf("config: front %.1f, rear %.1f, VL=%s VR=%s H=%s\n",
                thrFront, thrRear, at(VL).serial.c_str(),
                at(VR).serial.c_str(), at(HR).serial.c_str());
}

// Exchange two wheels' readings. Nothing disconnects: only the position->link
// table changes, so the readings move across instantly.
//
// The animation is the caller's business, because the two screens that swap
// draw their rows in different places -- and the caller is the only one that
// knows which of the two it is.
static void swapPositions(int a, int b) {
  if (a == b || a < 0 || b < 0) return;
  int t = posLink[a]; posLink[a] = posLink[b]; posLink[b] = t;
  savePositions();
  Serial.printf("swap %s/%s\n", POS_LABEL[a], POS_LABEL[b]);
}

// On the readings screen: the two tiles pass each other.
static void swapTiles(int a, int b) {
  if (a == b || a < 0 || b < 0) return;
  int old[NPOS]; memcpy(old, posLink, sizeof(old));
  swapPositions(a, b);
  animateCross(a, b, old);
}

// Move the entry that has been picked up one row down or up, the ends wrapping.
// Exchanging with the neighbour is what a move is in a list of three: the entry
// goes one way, whatever stood there comes the other, and the selection travels
// with the entry rather than staying on the row. Any two exchanges of three
// items reach all six arrangements, so nothing is out of reach.
static void moveSel(int dir) {
  int from = assignSel, to = (from + dir + NPOS) % NPOS;
  int old[NPOS]; memcpy(old, posLink, sizeof(old));
  swapPositions(from, to);
  assignSel = to;
  animateRows(from, to, old);
  uiDirty = true;
}

// Forget one wheel. The sensor is released and everything that described it
// goes with it -- including the leak reference, which belonged to that sensor
// and not to the wheel. The wheel then reads as empty until something is
// learned for it.
static void forgetPosition(int pos) {
  int i = posLink[pos];
  dropLink(i, "deleted");
  conn[i].serial    = "";
  conn[i].haveAddr  = false;
  conn[i].baseBar   = NAN;
  conn[i].baseTemp  = NAN;
  conn[i].leaking   = false;
  savePositions();
  Serial.printf("deleted %s\n", POS_LABEL[pos]);
}

// Candidates currently audible, strongest first. Sorting by RSSI only orders the
// offer -- it never decides. Inside a velomobile your own sensors sit behind the
// shell and a stranger's trike can be a metre away, so a signal-strength rule
// would eventually mis-assign silently, which is the worst failure for an alarm.
static int candidates(String* out, int maxOut) {
  struct { String n; int r; } tmp[MAX_SEEN];
  int n = 0;
  portENTER_CRITICAL(&seenMux);
  for (int i = 0; i < MAX_SEEN && n < MAX_SEEN; i++) {
    if (!seenTab[i].used || millis() - seenTab[i].t > SEEN_MS) continue;
    tmp[n].n = String(seenTab[i].name); tmp[n].r = seenTab[i].rssi; n++;
  }
  portEXIT_CRITICAL(&seenMux);
  for (int i = 1; i < n; i++)                      // insertion sort, n <= 10
    for (int j = i; j > 0 && tmp[j].r > tmp[j-1].r; j--) {
      auto t = tmp[j]; tmp[j] = tmp[j-1]; tmp[j-1] = t;
    }
  int m = 0;
  for (int i = 0; i < n && m < maxOut; i++) {
    bool taken = false;
    for (int k = 0; k < pickPos; k++) if (pickSel[k] == tmp[i].n) taken = true;
    if (!taken) out[m++] = tmp[i].n;
  }
  return m;
}
// Forget the whole assignment -- serials, addresses, references -- and listen
// afresh. Keeping any of it paired a wheel with last time's sensor.
// Everything that belongs to the old assignment, not just the serial. Keeping
// the addresses paired each wheel with whichever sensor it had last time: a log
// once showed a link connecting "directly" to the address of a completely
// different sensor and reporting it under the new name.
static void forgetAssignment() {
  for (int i = 0; i < NPOS; i++) {
    dropLink(i, "relearn");
    conn[i].serial    = "";
    conn[i].haveAddr  = false;
    conn[i].baseBar   = NAN;      // the reference belonged to the old sensor too
    conn[i].baseTemp  = NAN;
    conn[i].leaking   = false;
  }
  for (int i = 0; i < NPOS; i++) { posLink[i] = i; pickSel[i] = ""; }
  pickPos = 0; cand = 0;
  savePositions();
}

static void enterLearn() {
  forgetAssignment();
  learnUntil = millis() + LEARN_MS;
  mode = M_LEARN; uiDirty = true;
  Serial.println("learning: listening for sensors");
}
// Take the addresses from the scan that just found them, so the first
// connect is both direct and to the right sensor.
static void finishLearn() {
  for (int i = 0; i < NPOS; i++) {
    dropLink(i, "learned");
    conn[i].serial   = pickSel[i];
    conn[i].haveAddr = false;
    // Take each address from the scan that just found it, so the first connect
    // is direct and, above all, is to the right sensor.
    NimBLEAddress a;
    if (!conn[i].serial.isEmpty() && findSeen(conn[i].serial, a, SEEN_MS)) {
      conn[i].addr = a; conn[i].haveAddr = true;
    }
  }
  for (int i = 0; i < NPOS; i++) { posLink[i] = i; conn[i].nextTry = 0; conn[i].backoff = 500; }
  savePositions();
  mode = M_SHOW; uiDirty = true;
  Serial.printf("learned: VL=%s VR=%s H=%s\n", at(VL).serial.c_str(),
                at(VR).serial.c_str(), at(HR).serial.c_str());
}

// When the window closes: three or fewer is unambiguous, so assign them round
// the vehicle and say nothing. More than three cannot be resolved by any rule
// the device could apply, so it asks instead of guessing.
static void closeLearnWindow() {
  String list[MAX_SEEN];
  int n = candidates(list, MAX_SEEN);
  Serial.printf("learn window closed: %d sensor(s)\n", n);
  if (n <= NPOS) {
    for (int i = 0; i < NPOS; i++) pickSel[i] = (i < n) ? list[i] : String("");
    finishLearn();
  } else {
    pickPos = 0; cand = 0; mode = M_PICK; uiDirty = true;
  }
}

// While picking, the highlighted candidate is connected on link 0 so its live
// pressure is visible. A reading far from your own tyres settles the question
// faster than any amount of dBm -- and you can squeeze a tyre to be sure.
static void previewCandidate(const String& serial) {
  if (conn[0].serial == serial) return;
  dropLink(0, "preview");
  conn[0].serial = serial;
  conn[0].nextTry = 0; conn[0].backoff = 500;
}
