// The two buttons, and what a gesture means wherever it is made. The touch
// panel of the S3 is in touch.ino.
// Part of the airspy_trike sketch: the Arduino build concatenates every
// .ino in this folder, so the types and globals in airspy_trike.ino are
// visible here without a header.

// --- debounce and classify ----------------------------------------------- //
// A tap is reported on release and only when the press
// never grew into a long one, so holding for the menu cannot page on its way
// there. Nothing repeats and nothing needs both buttons: press again instead.
// That removed the guard against an imperfectly simultaneous A+B as well -- it
// had no combination left to protect.
#define LONG_MS 1000
#define HINT_MS 300
#define TAP_MIN 40

static Ev readButtons() {
  static bool aPrev = false, bPrev = false;
  static bool aLong = false, bLong = false, aHint = false, bHint = false;
  static uint32_t aDown = 0, bDown = 0;
  bool a = digitalRead(PIN_A) == LOW;
  bool b = digitalRead(PIN_B) == LOW;
  uint32_t now = millis();
  Ev ev = EV_NONE;

  if (a && !aPrev) { aDown = now; aLong = false; aHint = false; }
  if (b && !bPrev) { bDown = now; bLong = false; bHint = false; }

  if      (!a && aPrev && !aLong && now - aDown >= TAP_MIN) ev = EV_A_TAP;
  else if (!b && bPrev && !bLong && now - bDown >= TAP_MIN) ev = EV_B_TAP;
  else if (a && !aLong && now - aDown >= LONG_MS) { aLong = true; ev = EV_A_LONG; }
  else if (b && !bLong && now - bDown >= LONG_MS) { bLong = true; ev = EV_B_LONG; }
  else if (a && !aHint && now - aDown >= HINT_MS) { aHint = true; ev = EV_A_HINT; }
  else if (b && !bHint && now - bDown >= HINT_MS) { bHint = true; ev = EV_B_HINT; }

  aPrev = a; bPrev = b;
  return ev;
}

// --- what a gesture means, which depends on where we are ----------------- //
// Wandering off mid-setting should leave the readings on screen, not a menu.
// A threshold is stored on the way out, because both long presses store it and a
// timeout that threw the value away would be the one exit that behaved
// differently. Only from those two screens, so a forgotten menu costs no write.
static void settingsTimeout() {
  if (mode == M_THR_F || mode == M_THR_R) saveThresholds();
  assignHeld = false;                 // never leave an entry picked up
  mode = M_SHOW; showHelp = false; uiDirty = true;
  Serial.println("settings timed out");
}
// Choosing a page. The grammar is the same everywhere: short steps, long right
// goes in or confirms, long left goes back out. The readings are the top level,
// so there is nothing further out and a long left does nothing there.
static void menuChosen() {
  switch (menuSel) {
    case MI_ASSIGN: mode = M_ASSIGN; assignSel = 0; assignHeld = false; break;
    case MI_LEARN:  enterLearn();    return;
    case MI_THR_F:  mode = M_THR_F;  break;
    case MI_THR_R:  mode = M_THR_R;  break;
  }
  uiDirty = true;
}

// Deleting is reached from the list and returns to it: having removed one
// sensor you are most likely about to sort out the other two.
static void confirmed() {
  forgetPosition(assignSel);
  assignHeld = false;
  mode = M_ASSIGN; uiDirty = true;
}

static void handle(Ev ev) {
  if (ev == EV_NONE) return;
  lastInput = millis();

  // While the panel is dark, either button simply brings it back, and that
  // press is spent doing so. Acting on the same press would mean never seeing
  // what you reached for.
  static bool spent = false;
  if (!backlightOn()) {
    lightUntil = millis() + LIGHT_MS;
    spent = true; uiDirty = true;
    return;
  }
  if (spent) {
    if (ev == EV_A_TAP || ev == EV_B_TAP || ev == EV_A_LONG || ev == EV_B_LONG)
      spent = false;
    return;
  }
  lightUntil = millis() + LIGHT_MS;

  // Held for a moment: show what the two buttons do here. Let go and it goes.
  // The help sits over the bottom tile, so the tiles have to be repainted to
  // clear it again.
  if (ev == EV_A_HINT || ev == EV_B_HINT) {
    showHelp = true;
    for (int i = 0; i < NPOS; i++) prevValid[i] = false;
    uiDirty = true;
    return;
  }
  if (showHelp) {
    showHelp = false;
    for (int i = 0; i < NPOS; i++) prevValid[i] = false;
    uiDirty = true;
  }

  switch (mode) {
    case M_SHOW:
      if (ev == EV_A_LONG) { mode = M_MENU; menuSel = 0; uiDirty = true; }
      else if (ev == EV_B_TAP) {
        // Paging is for looking something up; you leave by asking for the
        // pressure back, so the pressure is not part of the cycle.
        shown = (shown == S_TEMP) ? S_BATT : (shown == S_BATT) ? S_SERIAL : S_TEMP;
        shownUntil = millis() + PAGE_MS;
        uiDirty = true;
      } else if (ev == EV_A_TAP) { shown = S_BAR; uiDirty = true; }
      break;

    case M_MENU:
      // A is forwards everywhere: the window moves right over the ring and the
      // item slides out to the left.
      if      (ev == EV_A_TAP)  menuStep(+1);
      else if (ev == EV_B_TAP)  menuStep(-1);
      else if (ev == EV_A_LONG) menuChosen();
      else if (ev == EV_B_LONG) { mode = M_SHOW; uiDirty = true; }
      break;

    case M_ASSIGN:
      // Two stages, same two buttons. Browsing, the selection moves; once an
      // entry has been picked up, the entry moves. A is down and B is up in
      // both, so the gesture does not change when the stage does -- only what
      // it grips.
      //
      // Deleting sits on B held, which everywhere else means "back out". That
      // collision is the reason the confirmation is on A: a reflex press of the
      // left button cannot reach past the dialog, because answering it needs
      // the other hand.
      if (!assignHeld) {
        if      (ev == EV_A_TAP)  { assignSel = (assignSel + 1) % NPOS; uiDirty = true; }
        else if (ev == EV_B_TAP)  { assignSel = (assignSel + NPOS - 1) % NPOS; uiDirty = true; }
        else if (ev == EV_A_LONG) { assignHeld = true; uiDirty = true; }
        else if (ev == EV_B_LONG) { mode = M_MENU; uiDirty = true; }
      } else {
        if      (ev == EV_A_TAP)  moveSel(+1);
        else if (ev == EV_B_TAP)  moveSel(-1);
        else if (ev == EV_A_LONG) { assignHeld = false; uiDirty = true; }
        else if (ev == EV_B_LONG) { mode = M_CONFIRM; uiDirty = true; }
      }
      break;

    case M_THR_F:
    case M_THR_R: {
      float* thr = (mode == M_THR_F) ? &thrFront : &thrRear;
      // Either button held stores and leaves. Both values are on screen while
      // you set them, so there is nothing a discard would rescue you from --
      // and one less thing to remember about which side does what.
      if      (ev == EV_A_TAP)  { *thr = constrain(*thr + 0.1f, THR_MIN, THR_MAX); uiDirty = true; }
      else if (ev == EV_B_TAP)  { *thr = constrain(*thr - 0.1f, THR_MIN, THR_MAX); uiDirty = true; }
      else if (ev == EV_A_LONG || ev == EV_B_LONG) {
        saveThresholds(); mode = M_MENU; uiDirty = true;
      }
      break;
    }

    case M_CONFIRM:
      // Only a long press either way, and the yes is on the button that did not
      // ask: nothing destructive should be possible by brushing a button, nor by
      // holding the same one twice.
      if      (ev == EV_A_LONG) confirmed();
      else if (ev == EV_B_LONG) { mode = M_ASSIGN; uiDirty = true; }
      break;

    case M_LEARN:
      if      (ev == EV_A_LONG) closeLearnWindow();          // impatient
      else if (ev == EV_B_LONG) { mode = M_SHOW; uiDirty = true; }
      break;

    case M_PICK: {
      String list[MAX_SEEN];
      int n = candidates(list, MAX_SEEN);
      if (ev == EV_B_TAP && n) { cand = (cand + 1) % n; uiDirty = true; }
      else if (ev == EV_A_LONG && n) {
        if (cand >= n) cand = 0;
        pickSel[pickPos++] = list[cand];
        cand = 0;
        if (pickPos >= NPOS) finishLearn(); else uiDirty = true;
      } else if (ev == EV_B_LONG) {                          // leave it empty
        pickSel[pickPos++] = "";
        cand = 0;
        if (pickPos >= NPOS) finishLearn(); else uiDirty = true;
      }
      break;
    }
  }
}
