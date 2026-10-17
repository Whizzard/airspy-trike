// The readings screen, and the primitives every screen draws with. The setup
// screens are in screens.ino.
// Part of the airspy_trike sketch: the Arduino build concatenates every
// .ino in this folder, so the types and globals in airspy_trike.ino are
// visible here without a header.

// --- the backlight, which is half of what this device draws -------------- //

// Measured on the T1: the backlight is 20 mA of a 40 mA steady state -- half of
// everything the device uses once connected. So it stays dark while every wheel
// is above its threshold, and comes on for anything worth looking at: an alarm,
// any settings screen, or simply a press, which buys ten seconds of reading.
static bool backlightOn() {
  // A warning holds the panel lit for as long as it stands. The slow-puncture
  // state used to set the one-minute window once and then expire, which meant
  // missing it by simply not looking for a minute.
  for (int i = 0; i < NPOS; i++) if (posAlarm(i) || conn[i].leaking) return true;
  return (mode != M_SHOW) || (int32_t)(millis() - lightUntil) < 0;
}
// Half of what this device draws is the backlight, so it is off unless there
// is something to see. Called every pass; only writes the pin on a change.
static void updateBacklight() {
  bool on = backlightOn();
  static int last = -1;
  if ((int)on != last) { last = on; digitalWrite(PIN_BL, on ? HIGH : LOW); }
}

// --- what counts as an alarm --------------------------------------------- //
// Here rather than with the sensor data, because an alarm has exactly two
// consequences and both of them are on this panel: a tile turns red, and the
// backlight stays on.
static float thrFor(int pos) { return pos == HR ? thrRear : thrFront; }

// Hysteresis: once raised, the alarm only clears 0.05 bar above the threshold.
// Without it a reading sitting exactly on the limit flickers in and out, which
// reads as a fault in the device rather than a warning about the tyre.
static bool posAlarm(int pos) {
  Link& s = at(pos);
  if (s.serial.isEmpty() || !s.haveData) { s.alarmed = false; return false; }
  if (millis() - s.lastRx > STALE_MS)    { return false; }
  float thr = thrFor(pos);
  s.alarmed = s.alarmed ? (s.bar < thr + 0.05f) : (s.bar < thr);
  return s.alarmed;
}

// --- text ---------------------------------------------------------------- //
// Centre text in a rectangle. Works for the built-in font and for a GFXfont,
// whose cursor is a baseline rather than a top-left corner -- getTextBounds
// returns the offset needed to reconcile the two.
static void drawCentredOn(Arduino_GFX& g, const char* txt, int x, int y, int w, int h,
                          const GFXfont* f, uint8_t size, uint16_t col) {
  g.setFont(f);
  g.setTextSize(size);
  g.setTextColor(col);
  int16_t x1, y1; uint16_t tw, th;
  g.getTextBounds(txt, 0, 0, &x1, &y1, &tw, &th);
  g.setCursor(x + (w - (int)tw) / 2 - x1, y + (h - (int)th) / 2 - y1);
  g.print(txt);
  g.setFont(NULL);
}

static void drawCentred(const char* txt, int x, int y, int w, int h,
                        const GFXfont* f, uint8_t size, uint16_t col) {
  drawCentredOn(*gfx, txt, x, y, w, h, f, size, col);
}

// A line that changes while its screen stays up, composed in the tile buffer
// and pushed in a single transfer.
//
// This is the whole of the flicker story. Clearing a region on the panel and
// then drawing into it leaves one frame of bare background, and a value that
// repaints twice a second turns that into a visible pulse. Nothing may clear
// the whole panel on an update either: drawUI() already clears once on the way
// in, so a screen only ever repaints the parts of itself that moved.
//
// The buffer belongs to the tiles, so a line is always the full panel width and
// at most one tile tall -- which every one of them is.
static void blitLine(const char* txt, int y, int h, const GFXfont* f,
                     uint8_t size, uint16_t col) {
  tileBuf->fillScreen(C_BLACK);
  drawCentredOn(*tileBuf, txt, 0, 0, SCR_W, h, f, size, col);
  gfx->draw16bitRGBBitmap(0, y, tileBuf->getFramebuffer(), SCR_W, h);
}

// The number carries the reading, a smaller unit sits behind it, and the pair is
// centred as a whole. Colour does the rest of the work: white for pressure,
// orange for temperature, green/amber/red for battery -- so a glance is enough
// even before the unit is read.
static void drawValueUnit(const char* val, const char* unit, uint16_t col, int yOff) {
  int16_t vx, vy, ux = 0, uy = 0; uint16_t vw, vh, uw = 0, uh = 0;
  tileBuf->setFont(&FreeSansBold24pt7b); tileBuf->setTextSize(1);
  tileBuf->getTextBounds(val, 0, 0, &vx, &vy, &vw, &vh);
  bool hasUnit = unit && *unit;
  if (hasUnit) {
    tileBuf->setFont(NULL); tileBuf->setTextSize(2);
    tileBuf->getTextBounds(unit, 0, 0, &ux, &uy, &uw, &uh);
  }
  int gap   = hasUnit ? 5 : 0;
  int total = vw + gap + uw;
  int x = (TILE_W - total) / 2;
  int y = (TILE_H - vh) / 2 + yOff;

  tileBuf->setFont(&FreeSansBold24pt7b); tileBuf->setTextSize(1);
  tileBuf->setTextColor(col);
  tileBuf->setCursor(x - vx, y - vy);
  tileBuf->print(val);

  if (hasUnit) {                       // sit the unit on the digits' baseline
    tileBuf->setFont(NULL); tileBuf->setTextSize(2);
    tileBuf->setCursor(x + vw + gap, y + vh - uh);
    tileBuf->print(unit);
    tileBuf->setFont(NULL);
  }
}

// --- the tiles ----------------------------------------------------------- //
// What a tile says for one wheel. Pulled out of drawTile so the slide animation
// can ask the same question of a wheel that is on its way somewhere else.
//
// An empty string means no sensor is assigned to that wheel at all; a dash means
// one is assigned and currently saying nothing. Battery and temperature are
// unknown while a sensor is silent, and three pages of identical dashes tell you
// nothing, so those pages fall back to the serial -- which comes from the stored
// assignment and is therefore always available.
static void tileText(Link& s, char* txt, size_t n, const char** unit, bool* asSerial) {
  bool live = s.haveData && (millis() - s.lastRx < STALE_MS);
  *unit = "";
  *asSerial = (shown == S_SERIAL) || (!live && shown != S_BAR);
  if (s.serial.isEmpty()) { txt[0] = 0; return; }
  if (*asSerial) {
    int dash = s.serial.indexOf('-');       // the engraved number is the tail
    snprintf(txt, n, "%s", dash >= 0 ? s.serial.c_str() + dash + 1 : s.serial.c_str());
    return;
  }
  if (!live) { strcpy(txt, "-"); return; }
  switch (shown) {
    case S_BATT: snprintf(txt, n, "%.0f", isnan(s.batt)  ? 0.0f : s.batt);
                 *unit = "%"; break;
    case S_TEMP: snprintf(txt, n, "%.1f", isnan(s.tempC) ? 0.0f : s.tempC);
                 *unit = "C"; break;
    default:     snprintf(txt, n, "%.2f", s.bar);
  }
}
// One wheel's tile: composed in RAM, pushed in a single transfer, and only
// when something actually changed. Painting straight to the panel meant
// erasing and redrawing the digits several times a second.
static void drawTile(int pos, int x, int y) {
  Link& s = at(pos);
  bool alarm = posAlarm(pos);
  bool live0 = s.haveData && (millis() - s.lastRx < STALE_MS);
  bool phase = (millis() / 500) % 2 == 0;
  // While peeking at temperature or battery the pressure alarm stops flashing --
  // a blinking background behind an unrelated number just looks broken. The one
  // battery now says everything it needs to through the length and colour of its
  // bar, so nothing else flashes.
  bool blink = phase && alarm && shown == S_BAR;
  bool mark  = false;
#if HAS_TOUCH
  mark = (pos == selTile);                    // held for a swap
#endif
  bool live  = live0;
  bool leak  = live0 && s.leaking;
  // The button labels sit over the bottom tile, so they belong to its transfer.
  // Drawn on top afterwards they were erased and redrawn every time the tile
  // repainted -- the blink that used to appear while a button was held.
  bool help  = showHelp && pos == NPOS - 1;

  char txt[12]; const char* unit; bool asSerial;
  tileText(s, txt, sizeof(txt), &unit, &asSerial);

  // Kept as a percentage rather than a band: the bar is drawn to that length, so
  // the redraw check has to notice every step of it.
  uint8_t battPct = 255;                     // 255 = unknown
  if (live && !isnan(s.batt)) battPct = (uint8_t)constrain((int)s.batt, 0, 100);
  bool battLow = (battPct != 255) && (battPct <= (uint8_t)BATT_WARN);

  // Nothing changed -> leave the panel alone. This is what removes the flicker.
  if (prevValid[pos] && !strcmp(prevTile[pos].txt, txt) && prevTile[pos].alarm == alarm
      && prevTile[pos].blink == blink && prevTile[pos].mark == mark
      && prevTile[pos].batt == battPct && prevTile[pos].what == (uint8_t)shown
      && prevTile[pos].leak == leak && prevTile[pos].help == help) return;
  strcpy(prevTile[pos].txt, txt);
  prevTile[pos].alarm = alarm; prevTile[pos].blink = blink;
  prevTile[pos].mark = mark;   prevTile[pos].batt = battPct;
  prevTile[pos].leak = leak;   prevTile[pos].help = help;
  prevTile[pos].what = (uint8_t)shown;
  prevValid[pos] = true;

  uint16_t bg = blink ? C_RED : C_BLACK;
  uint16_t fg;
  switch (shown) {
    case S_BATT: fg = battLow ? C_RED : C_GREEN; break;
    case S_TEMP:   fg = C_ORANGE; break;
    case S_SERIAL: fg = C_CYAN;   break;
    // falls through to the pressure colours otherwise
    // Amber, not the alarm's red: losing air slowly matters, but it is not the
    // same kind of news as being below the threshold, and should not look it.
    default:     fg = alarm ? C_RED : leak ? C_YELLOW : C_WHITE;
  }
  if (asSerial) fg = C_CYAN;          // a serial standing in is not a reading
  if (blink) fg = C_BLACK;
  if (!live) fg = C_DIM;
  // Which tiles just moved. A frame was the obvious choice and the wrong one: at
  // the screen edge it gets clipped and looks like a drawing fault. A tinted
  // panel cannot be clipped and reads at a glance.
  if (mark && !alarm) { bg = C_GREY; fg = C_BLACK; }

  tileBuf->fillScreen(bg);

  // Battery as a line under the reading, green and as long as the charge is
  // high. Below the warning level it goes full width in red instead: drawn to
  // length it would be the one bar you cannot see, exactly when it matters.
  if (battPct != 255) {
    const int margin = 10, avail = TILE_W - 2 * margin;
    int w = battLow ? avail : (avail * battPct) / 100;
    if (w < 2) w = 2;
    tileBuf->fillRect(margin, TILE_H - 10, w, 5,
                      blink ? C_BLACK : (battLow ? C_RED : C_GREEN));
  }

  // Stacked vertically, nothing says which front wheel is which any more. A mark
  // on the outer edge restores it: left wheel marked left, right wheel right.
  if (pos == VL || pos == VR) {
    uint16_t mc = blink ? C_BLACK : C_GREY;
    tileBuf->fillRect(pos == VL ? 0 : TILE_W - 7, (TILE_H - 56) / 2, 7, 56, mc);
  }

  if (txt[0]) {
    // The serial is five digits wide; at the big face that would not fit the
    // tile, so it gets the built-in font instead.
    if (asSerial) drawCentredOn(*tileBuf, txt, 0, 0, TILE_W, TILE_H, NULL, 4, fg);
    else          drawValueUnit(txt, unit, fg, 0);
  }

  // Only the right-hand label: there is nothing further out than the readings,
  // so a long left does nothing and promising otherwise would be a lie.
  if (help) drawSymOn(*tileBuf, TILE_W - 18, TILE_H - 16, 10, SY_ENTER);

  gfx->draw16bitRGBBitmap(x, y, tileBuf->getFramebuffer(), TILE_W, TILE_H);
}

// --- button symbols ------------------------------------------------------ //
// Shapes big enough to see at arm's length: a triangle to go in or back out, a
// tick to confirm, a cross to decline, two more triangles for up and down, and
// a bin for the one gesture that throws something away.
static void thickLine(Arduino_GFX& g, int x0, int y0, int x1, int y1, int w, uint16_t col) {
  for (int o = -w / 2; o <= w / 2; o++) {
    g.drawLine(x0 + o, y0, x1 + o, y1, col);
    g.drawLine(x0, y0 + o, x1, y1 + o, col);
  }
}

static void drawSymOn(Arduino_GFX& g, int cx, int cy, int r, Sym s) {
  uint16_t col = (s == SY_TRASH) ? C_RED : (s == SY_OK) ? C_GREEN : C_WHITE;
  switch (s) {
    case SY_BACK:  g.fillTriangle(cx + r, cy - r, cx + r, cy + r, cx - r, cy, col); break;
    case SY_ENTER: g.fillTriangle(cx - r, cy - r, cx - r, cy + r, cx + r, cy, col); break;
    case SY_UP:    g.fillTriangle(cx - r, cy + r, cx + r, cy + r, cx, cy - r, col); break;
    case SY_DOWN:  g.fillTriangle(cx - r, cy - r, cx + r, cy - r, cx, cy + r, col); break;
    case SY_OK:
      thickLine(g, cx - r, cy, cx - r / 3, cy + r - 2, 4, col);
      thickLine(g, cx - r / 3, cy + r - 2, cx + r, cy - r, 4, col);
      break;
    case SY_NO:
      thickLine(g, cx - r, cy - r, cx + r, cy + r, 4, col);
      thickLine(g, cx + r, cy - r, cx - r, cy + r, 4, col);
      break;
    case SY_PLUS:
      g.fillRect(cx - r, cy - 2, 2 * r + 1, 5, col);
      g.fillRect(cx - 2, cy - r, 5, 2 * r + 1, col);
      break;
    case SY_MINUS: g.fillRect(cx - r, cy - 2, 2 * r + 1, 5, col); break;
    case SY_TRASH:
      g.fillRect(cx - 3, cy - r - 2, 6, 3, col);                  // the handle
      g.fillRect(cx - r, cy - r + 1, 2 * r, 3, col);              // the lid
      g.drawRect(cx - r + 2, cy - r + 6, 2 * r - 4, 2 * r - 6, col);
      break;
    default: break;
  }
}

// The two button labels, each sitting over its own button: left is B, right A.
static void drawHelp(Sym left, Sym right) {
  int y = SCR_H - 16;
  gfx->fillRect(0, y - 14, SCR_W, 28, C_BLACK);
  drawSymOn(*gfx, 18, y, 10, left);
  drawSymOn(*gfx, SCR_W - 18, y, 10, right);
}

static void drawShow() {
  for (int i = 0; i < NPOS; i++) drawTile(i, 0, TILE_Y[i]);
}

// --- swapping two tiles -------------------------------------------------- //
// Only the touch panel swaps tiles on the readings screen; the button route is
// the list in screens.ino, which has an animation of its own.
// The outer edge mark that says which front wheel a tile is. Stacked
// vertically, nothing else distinguishes them.
static void paintMark(int pos) {
  if (pos == VL || pos == VR)
    tileBuf->fillRect(pos == VL ? 0 : TILE_W - 7, (TILE_H - 56) / 2, 7, 56, C_GREY);
}
// A wheel's reading, displaced vertically. The tile buffer clips, so this is
// all an animation needs to slide one reading past another.
static void paintText(Link& s, int yOff) {
  char txt[12]; const char* unit; bool asSerial;
  tileText(s, txt, sizeof(txt), &unit, &asSerial);
  if (!txt[0]) return;
  bool live = s.haveData && (millis() - s.lastRx < STALE_MS);
  uint16_t fg = !live ? C_DIM : (asSerial ? C_CYAN : C_WHITE);
  if (asSerial) drawCentredOn(*tileBuf, txt, 0, yOff, TILE_W, TILE_H, NULL, 4, fg);
  else          drawValueUnit(txt, unit, fg, yOff);
}

// A swap sends the two readings past each other: the upper one down, the lower
// one up, overlapping in the middle. Folding them over an axis looked like an
// effect; this looks like what it is.
static void animateCross(int a, int b, const int oldLink[NPOS]) {
  if (a > b) { int t = a; a = b; b = t; }
  const int FRAMES = 9;
  for (int f = 1; f <= FRAMES; f++) {
    int d = TILE_H * f / FRAMES;
    for (int k = 0; k < 2; k++) {
      int p = k ? b : a;
      int out = (p == a) ?  d : -d;                // the upper one leaves downwards
      int in  = (p == a) ? (TILE_H - d) : (d - TILE_H);
      tileBuf->fillScreen(C_BLACK);
      paintMark(p);
      paintText(conn[oldLink[p]], out);
      paintText(conn[posLink[p]], in);
      gfx->draw16bitRGBBitmap(0, TILE_Y[p], tileBuf->getFramebuffer(), TILE_W, TILE_H);
    }
    delay(18);
  }
  for (int i = 0; i < NPOS; i++) prevValid[i] = false;
}
// --- dispatch ------------------------------------------------------------ //
// Draw whichever screen the mode calls for, clearing first on a change.
static void drawUI() {
  if (mode != lastMode) {
    gfx->fillScreen(C_BLACK); lastMode = mode; drawEpoch++;
    for (int i = 0; i < NPOS; i++) prevValid[i] = false;   // cache is stale
  }
  switch (mode) {
    case M_SHOW:   drawShow();        break;
    case M_MENU:   drawMenu();        break;
    case M_ASSIGN: drawAssign();      break;
    case M_CONFIRM: drawConfirm();    break;
    case M_THR_F:
    case M_THR_R:  drawThresholds();  break;
    case M_LEARN:  drawLearn();       break;
    case M_PICK:   drawPick();        break;
  }
}
