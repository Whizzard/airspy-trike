// The setup screens: the menu and everything reached from it. None of this is
// on the panel while the vehicle is moving.
// Part of the airspy_trike sketch: the Arduino build concatenates every
// .ino in this folder, so the types and globals in airspy_trike.ino are
// visible here without a header, and the drawing primitives in ui.ino are too.
//
// They appear below in the order the menu offers them.

// --- the icons that stand for the four pages ----------------------------- //
// Menu icons: one thick sign per page, drawn rather than fonted, each large
// enough to read across a handlebar without looking for it. Fine detail is
// wasted here -- at a sixth of the panel's height a figure either reads as a
// shape or not at all, and the word underneath says the rest.
//
// The two thresholds are the vehicle seen from above, two wheels at the front
// and one at the back, with the pair the page guards lit and the rest left
// dim. The only thing that differs between those two pages is which wheels
// they are about, so that is the whole of the icon -- an arrow onto a line or
// a gauge said "pressure, somewhere, somehow" and left the distinction to the
// caption underneath.
static void drawIcon(Arduino_GFX& g, int cx, int cy, int r, Icon ic, uint16_t col) {
  switch (ic) {
    case IC_ASSIGN: {                     // a spanner: the page you set up on
      int d = r * 3 / 5, w = r / 3;
      int x0 = cx - d, y0 = cy + d;       // ring end, lower left
      int x1 = cx + d, y1 = cy - d;       // open end, upper right
      thickLine(g, x0, y0, x1, y1, w, col);
      g.fillCircle(x0, y0, w, col);
      g.fillCircle(x0, y0, w / 2, C_BLACK);
      g.fillCircle(x1, y1, w, col);
      // The jaw: a wedge cut out of the open end, facing away from the shaft.
      g.fillTriangle(x1 - w / 4, y1 + w / 4,
                     x1 + w * 14 / 10, y1 - w * 6 / 10,
                     x1 + w * 6 / 10,  y1 - w * 14 / 10, C_BLACK);
      break;
    }
    case IC_LEARN: {                      // a plus: one more sensor
      int w = 13;
      g.fillRect(cx - r, cy - w / 2, 2 * r, w, col);
      g.fillRect(cx - w / 2, cy - r, w, 2 * r, col);
      break;
    }
    case IC_THR_F:
    case IC_THR_R: {
      // Each wheel is a bar lying along the direction of travel, which is up.
      bool front = (ic == IC_THR_F);
      uint16_t f = front ? col : C_DIM, b = front ? C_DIM : col;
      int w = r / 3, h = r * 2 / 3;
      g.fillRect(cx - r * 3 / 5 - w / 2, cy - r / 2 - h / 2, w, h, f);
      g.fillRect(cx + r * 3 / 5 - w / 2, cy - r / 2 - h / 2, w, h, f);
      g.fillRect(cx - w / 2,             cy + r / 2 - h / 2, w, h, b);
      break;
    }
  }
}

// --- the menu itself ----------------------------------------------------- //
// One item per page, with a drawn icon and its name underneath. A list of all
// of them at once meant six-pixel type, which on this panel is decoration
// rather than information.
//
// There is no "clear" page any more. Learning begins by forgetting, so clearing
// the assignment was learning without the useful half, and the list deletes a
// single wheel where that is actually wanted.
static const char* MENU_1[MI_COUNT] = {
  "Zuordnung", "Schwelle", "Schwelle", "Anlernen"
};
static const char* MENU_2[MI_COUNT] = {
  "", "", "", ""                      // the lit wheels say which threshold
};
static const Icon MENU_ICON[MI_COUNT] = {
  IC_ASSIGN, IC_THR_F, IC_THR_R, IC_LEARN
};

#define BAND_Y (SCR_H * 15 / 100)
#define BAND_H (SCR_H * 55 / 100)
static Arduino_Canvas* menuBand = nullptr;

// Allocated the first time the menu is opened, and kept: a third of the panel
// at two bytes a pixel is not worth holding for a device that may never be set
// up again, but it is worth not reallocating once it has been.
static bool menuBandReady() {
  if (!menuBand) {
    menuBand = new Arduino_Canvas(SCR_W, BAND_H, nullptr);
    if (menuBand && !menuBand->begin()) { delete menuBand; menuBand = nullptr; }
  }
  return menuBand != nullptr;
}

// One item's icon and name, displaced. The canvas clips, so drawing the
// outgoing item one way and the incoming the other is the whole animation.
//
// yOff exists for the fallback path only: into the band canvas the band's own
// origin is zero, but straight onto the panel it is BAND_Y. Painting the
// fallback without it put the icon a third of a screen too high -- which is
// what the first frame after opening the menu used to do.
static void paintMenuBand(Arduino_GFX& g, int item, int xOff, int yOff) {
  int cx = SCR_W / 2 + xOff;
  drawIcon(g, cx, yOff + BAND_H * 30 / 100, SCR_H * 15 / 100, MENU_ICON[item], C_CYAN);
  drawCentredOn(g, MENU_1[item], xOff, yOff + BAND_H * 62 / 100,
                SCR_W, 20, NULL, 2, C_WHITE);
  if (*MENU_2[item])
    drawCentredOn(g, MENU_2[item], xOff, yOff + BAND_H * 62 / 100 + 24,
                  SCR_W, 20, NULL, 2, C_WHITE);
}

static void drawMenu() {
  static int last = -1; static uint16_t ep = 0xFFFF;
  if (menuSel == last && ep == drawEpoch) return;
  bool arrived = (ep != drawEpoch);
  last = menuSel; ep = drawEpoch;

  // No full clear: drawUI() did that on the way in, and doing it again on every
  // step blacked the whole panel for a frame -- the flash at the end of the
  // slide. The band blit covers its own area, and the dots clear their strip.
  if (menuBandReady()) {
    menuBand->fillScreen(C_BLACK);
    paintMenuBand(*menuBand, menuSel, 0, 0);
    gfx->draw16bitRGBBitmap(0, BAND_Y, menuBand->getFramebuffer(), SCR_W, BAND_H);
  } else {
    paintMenuBand(*gfx, menuSel, 0, BAND_Y);   // without the buffer, simply flat
  }

  // Where you are in the ring, without a word of text. The others are filled
  // grey rather than outlined dim: a three-pixel ring in the darkest grey the
  // panel has is invisible at anything but a dead-on angle, and a position
  // indicator you cannot count is not one.
  int dy = SCR_H * 78 / 100, gap = 14;
  int x0 = SCR_W / 2 - (MI_COUNT - 1) * gap / 2;
  gfx->fillRect(0, dy - 5, SCR_W, 11, C_BLACK);
  for (int i = 0; i < MI_COUNT; i++)
    gfx->fillCircle(x0 + i * gap, dy, i == menuSel ? 4 : 3,
                    i == menuSel ? C_CYAN : C_GREY);
  if (arrived) drawHelp(SY_BACK, SY_ENTER);    // the same two all the way round
}

// Step to the next or previous item, sliding the one in from the side it comes
// from. A ring is easy to lose your place in; seeing which way it moves is most
// of what keeps that straight. Forwards is A, and forwards means the window
// travels right while the item leaves to the left.
static void menuStep(int dir) {
  int from = menuSel;
  int to   = (menuSel + dir + MI_COUNT) % MI_COUNT;
  if (menuBandReady()) {
    const int FRAMES = 7;
    for (int f = 1; f <= FRAMES; f++) {
      int d = SCR_W * f / FRAMES;
      menuBand->fillScreen(C_BLACK);
      paintMenuBand(*menuBand, from, -dir * d, 0);
      paintMenuBand(*menuBand, to,   -dir * d + dir * SCR_W, 0);
      gfx->draw16bitRGBBitmap(0, BAND_Y, menuBand->getFramebuffer(), SCR_W, BAND_H);
      delay(14);
    }
  }
  menuSel = to;
  uiDirty = true;
}

// The assignment screen is a list of the three wheels, one row each, showing
// what actually tells the sensors apart -- all of it live: the engraved number
// to match against the valve, the pressure so a squeeze on a tyre identifies
// its wheel, and the temperature, because one wheel in the sun reads
// differently from one in the shade.
//
// Two stages, told apart by the frame. A grey frame is a selection moving over
// the rows; a cyan one with an arrow at each edge is an entry that has been
// picked up and now moves itself, the arrows sitting over the buttons that
// move it.
// --- assignment ---------------------------------------------------------- //
// Three rows and nothing else. The title went: "ZUORDNUNG" told you what you
// had just chosen from a menu, and it cost the readings a quarter of their
// height. The row the frame is on is the only thing this screen is about.
//
// ROW_Y0 is a real margin, not a hairline. The first row's frame is the only
// thing this device ever draws within a few pixels of the top edge, and three
// was close enough that it was impossible to tell a clipped panel from a tight
// case. Holding A at start-up now settles that question; see fakeBegin().
#define ROW_Y0 8
#define ROW_DY 66
#define ROW_H  62

// Which wheel the row is, in the corner. It belongs to the row, so it stays put
// while an entry slides through.
static void paintRowLabel(int p) {
  drawCentredOn(*tileBuf, POS_LABEL[p], 8, 5, 16, 8, NULL, 1, C_GREY);
}

// The frame round the selected entry, at the entry's own displacement, and the
// two arrows inside it when it has been picked up. Three pixels thick and white
// rather than grey: a single grey line at arm's length on a moving vehicle is
// not a frame, it is a suspicion.
//
// It takes a vertical offset because it travels with the entry it belongs to --
// a frame that stays behind while its contents leave marks the slot rather than
// the sensor, and the sensor is what is being moved.
static void paintRowFrame(int yOff, bool held) {
  uint16_t fr = held ? C_CYAN : C_WHITE;
  for (int k = 0; k < 3; k++)
    tileBuf->drawRect(k, yOff + k, SCR_W - 2 * k, ROW_H - 2 * k, fr);
  // Beside the serial, not beside the reading: the pressure and temperature
  // line runs 105 of the panel's 135 pixels, so arrows on its row sat on top
  // of the digits. The serial is 90 wide and leaves room either side.
  if (held) {                           // over the buttons that move it
    drawSymOn(*tileBuf, 11, yOff + 16, 6, SY_UP);
    drawSymOn(*tileBuf, SCR_W - 11, yOff + 16, 6, SY_DOWN);
  }
}

// One entry's contents, displaced vertically. The buffer clips, so drawing the
// one leaving one way and the one arriving the other is the whole animation.
//
// The colours are the readings screen's own: cyan for a serial, white for a
// pressure, orange for a temperature. The alarm red is deliberately not among
// them -- a setup list is not where a warning belongs, and the number here is
// for telling three sensors apart, not for judging a tyre.
static void paintRowText(Link& s, int yOff) {
  bool live = s.haveData && (millis() - s.lastRx < STALE_MS);
  const char* dash = s.serial.indexOf('-') >= 0
                   ? s.serial.c_str() + s.serial.indexOf('-') + 1 : s.serial.c_str();
  drawCentredOn(*tileBuf, s.serial.isEmpty() ? "--" : dash, 0, yOff + 3, SCR_W, 26,
                NULL, 3, s.serial.isEmpty() ? C_DIM : C_CYAN);
  if (!live) {
    drawCentredOn(*tileBuf, "-", 0, yOff + 32, SCR_W, 26, NULL, 3, C_DIM);
    return;
  }
  // Pressure and temperature on one line, set by hand: two sizes and two
  // colours in one row is what tells them apart without a unit behind either.
  char pb[12], tb[8];
  snprintf(pb, sizeof(pb), "%.2f", s.bar);
  snprintf(tb, sizeof(tb), "%.0f", isnan(s.tempC) ? 0.0f : s.tempC);
  int pw = (int)strlen(pb) * 18, tw = (int)strlen(tb) * 12, gap = 9;
  int x = (SCR_W - (pw + gap + tw)) / 2;
  tileBuf->setFont(NULL);
  tileBuf->setTextSize(3); tileBuf->setTextColor(C_WHITE);
  tileBuf->setCursor(x, yOff + 32);
  tileBuf->print(pb);
  tileBuf->setTextSize(2); tileBuf->setTextColor(C_ORANGE);
  tileBuf->setCursor(x + pw + gap, yOff + 32 + 8);   // on the digits' baseline
  tileBuf->print(tb);
  tileBuf->setTextSize(1);
}

// A move sends the two entries past each other inside their rows: in the upper
// row the one leaving goes down and the one arriving comes up from below, and
// the other way round in the lower row. The frame stays on the row being moved
// into for the whole slide, so it reads as an entry travelling into a slot
// rather than two values trading places.
//
// Same shape as animateCross on the readings screen -- what differs is only the
// geometry and which painters compose a row, which is not enough shared
// structure to be worth a function pointer each.
static void animateRows(int from, int to, const int oldLink[NPOS]) {
  int a = from < to ? from : to;               // the upper of the two rows
  int b = from < to ? to   : from;
  int moved = oldLink[from];                   // the entry with the frame on it
  const int FRAMES = 7;
  for (int f = 1; f <= FRAMES; f++) {
    int d = ROW_H * f / FRAMES;
    for (int k = 0; k < 2; k++) {
      int p = k ? b : a;
      int out = (p == a) ?  d : -d;             // the upper one leaves downwards
      int in  = (p == a) ? (ROW_H - d) : (d - ROW_H);
      tileBuf->fillScreen(C_BLACK);
      paintRowLabel(p);
      // Frame and contents at the same displacement, so the two travel as one
      // thing. In the row it is leaving the moved entry carries the frame out;
      // in the row it is entering, the frame comes in with it.
      if (oldLink[p] == moved) paintRowFrame(out, assignHeld);
      paintRowText(conn[oldLink[p]], out);
      if (posLink[p] == moved) paintRowFrame(in, assignHeld);
      paintRowText(conn[posLink[p]], in);
      gfx->draw16bitRGBBitmap(0, ROW_Y0 + p * ROW_DY, tileBuf->getFramebuffer(),
                              SCR_W, ROW_H);
    }
    delay(16);
  }
}

static void drawAssign() {
  static uint16_t ep = 0xFFFF;
  static char lastRow[NPOS][48];
  static int  lastHeld = -1;

  if (ep != drawEpoch) {                // arrived: nothing fixed to put up
    ep = drawEpoch;
    for (int p = 0; p < NPOS; p++) lastRow[p][0] = 0;
    lastHeld = -1;
  }
  if (lastHeld != (int)assignHeld) {    // the buttons mean something else now
    lastHeld = (int)assignHeld;
    drawHelp(assignHeld ? SY_TRASH : SY_BACK, assignHeld ? SY_OK : SY_ENTER);
  }

  // Row by row, and only the rows that moved. Live pressure changes twice a
  // second, so repainting the screen for it blacked all three rows every time
  // one digit moved.
  for (int p = 0; p < NPOS; p++) {
    Link& s = at(p);
    bool sel  = (p == assignSel);
    bool live = s.haveData && (millis() - s.lastRx < STALE_MS);
    char sig[48];
    snprintf(sig, sizeof(sig), "%d%d%d%s%.2f%.0f", (int)sel, (int)assignHeld,
             (int)live, s.serial.c_str(),
             isnan(s.bar) ? 0.0f : s.bar, isnan(s.tempC) ? 0.0f : s.tempC);
    if (!strcmp(sig, lastRow[p])) continue;
    strncpy(lastRow[p], sig, sizeof(lastRow[p]) - 1);
    lastRow[p][sizeof(lastRow[p]) - 1] = 0;

    tileBuf->fillScreen(C_BLACK);
    paintRowLabel(p);
    if (sel) paintRowFrame(0, assignHeld);
    paintRowText(s, 0);
    gfx->draw16bitRGBBitmap(0, ROW_Y0 + p * ROW_DY, tileBuf->getFramebuffer(),
                            SCR_W, ROW_H);
  }
}

// --- deleting one wheel -------------------------------------------------- //
// Deleting one wheel's sensor. It names what it is about to throw away, because
// the only thing distinguishing the three rows is the number -- and the yes sits
// on the right button while the question was asked with the left.
static void drawConfirm() {
  static int last = -1; static uint16_t ep = 0xFFFF;
  if (assignSel == last && ep == drawEpoch) return;
  last = assignSel; ep = drawEpoch;

  Link& s = at(assignSel);
  int dash = s.serial.indexOf('-');
  drawCentred("LOESCHEN?", 0, 54, SCR_W, 20, NULL, 2, C_RED);
  drawCentred(s.serial.isEmpty() ? "--"
              : (dash >= 0 ? s.serial.c_str() + dash + 1 : s.serial.c_str()),
              0, 88, SCR_W, 26, NULL, 3, C_WHITE);
  drawCentred(POS_SHORT[assignSel], 0, 126, SCR_W, 18, NULL, 2, C_GREY);
  drawHelp(SY_NO, SY_OK);
}

// --- the two thresholds -------------------------------------------------- //
// Both thresholds are on screen at once, the one being edited framed. Seeing the
// other value while adjusting is what makes the pair meaningful -- front and rear
// run at different pressures, and you set them in relation to each other.
//
// Redrawn only on an actual change: nothing here animates, and repainting it
// every 60 ms was what made the digits flicker.
#define THR_BOX_H 72
static void drawThresholds() {
  static float lastF = -1, lastR = -1;
  static Mode  lastM = M_SHOW;
  static uint16_t ep = 0xFFFF;
  if (thrFront == lastF && thrRear == lastR && mode == lastM && ep == drawEpoch) return;
  bool arrived = (ep != drawEpoch);
  lastF = thrFront; lastR = thrRear; lastM = mode; ep = drawEpoch;

  if (arrived) {
    drawCentred("SCHWELLE", 0, 16, SCR_W, 16, NULL, 2, C_CYAN);
    // The two signs over the two buttons are the whole instruction; the line of
    // text that used to spell it out said the same thing twice.
    drawHelp(SY_MINUS, SY_PLUS);
  }

  // No "front"/"rear" captions: the order mirrors the main screen, where the
  // front wheels are above and the rear one below. Frame and number go in one
  // transfer, so a press does not flash the box it is changing.
  bool front = (mode == M_THR_F);
  for (int i = 0; i < 2; i++) {
    bool  active = (i == 0) == front;
    float v      = (i == 0) ? thrFront : thrRear;
    char t[12]; snprintf(t, sizeof(t), "%.1f", v);
    tileBuf->fillScreen(C_BLACK);
    if (active) tileBuf->drawRect(2, 0, SCR_W - 4, THR_BOX_H, C_CYAN);
    drawCentredOn(*tileBuf, t, 0, 10, SCR_W, 52, &FreeSansBold24pt7b, 1,
                  active ? C_YELLOW : C_DIM);
    gfx->draw16bitRGBBitmap(0, 48 + i * 80, tileBuf->getFramebuffer(),
                            SCR_W, THR_BOX_H);
  }
}

// --- learning ------------------------------------------------------------ //
// The learning window: how many sensors are in hand, and how long is left.
static void drawLearn() {
  String list[MAX_SEEN];
  int n = candidates(list, MAX_SEEN);
  int rest = (int)((learnUntil - millis()) / 1000) + 1;
  static int lastN = -1, lastRest = -1;
  static uint16_t ep = 0xFFFF;
  if (n == lastN && rest == lastRest && ep == drawEpoch) return;
  bool arrived = (ep != drawEpoch);
  lastN = n; lastRest = rest; ep = drawEpoch;

  if (arrived) {
    drawCentred("ANLERNEN", 0, 22, SCR_W, 20, NULL, 2, C_CYAN);
    drawHelp(SY_NO, SY_OK);
  }
  char b[40]; snprintf(b, sizeof(b), "%d", n);
  blitLine(b, 70, 56, &FreeSansBold24pt7b, 1, n ? C_GREEN : C_YELLOW);
  blitLine(n == 1 ? "Sensor" : "Sensoren", 130, 16, NULL, 2, C_GREY);
  snprintf(b, sizeof(b), "%d s", rest < 0 ? 0 : rest);
  blitLine(b, 180, 20, NULL, 2, C_WHITE);
}
// Choosing by hand, which only happens when more than three were found. The
// highlighted candidate is connected live so its pressure can settle it.
static void drawPick() {
  String list[MAX_SEEN];
  int n = candidates(list, MAX_SEEN);
  if (cand >= n) cand = 0;
  bool live = conn[0].haveData && millis() - conn[0].lastRx < STALE_MS;

  static char lastSig[48] = "";
  char sig[48];
  snprintf(sig, sizeof(sig), "%d|%d|%d|%d|%.2f", pickPos, cand, n, (int)live,
           live ? conn[0].bar : -1.0f);
  static uint16_t ep = 0xFFFF;
  if (!strcmp(sig, lastSig) && ep == drawEpoch) return;
  bool arrived = (ep != drawEpoch);
  strcpy(lastSig, sig); ep = drawEpoch;

  if (arrived) {
    drawCentred("AUSWAHL", 0, 8, SCR_W, 16, NULL, 2, C_CYAN);
    drawHelp(SY_NO, SY_OK);
  }
  blitLine(POS_SHORT[pickPos], 32, 18, NULL, 2, C_WHITE);
  if (n == 0) { blitLine("nichts da", 100, 16, NULL, 2, C_YELLOW); return; }

  previewCandidate(list[cand]);
  // Only the serial differs between candidates, so show that part large.
  String serial = list[cand];
  int dash = serial.indexOf('-');
  blitLine(dash >= 0 ? serial.substring(dash + 1).c_str() : serial.c_str(),
           76, 26, NULL, 3, C_GREEN);
  char b[32];
  if (live) snprintf(b, sizeof(b), "%.2f bar", conn[0].bar);
  else      strcpy(b, "verbinde...");
  blitLine(b, 120, 18, NULL, 2, C_WHITE);
  snprintf(b, sizeof(b), "%d / %d", cand + 1, n);
  blitLine(b, 150, 16, NULL, 2, C_GREY);
}
