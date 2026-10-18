// AIRSPY tyre-pressure monitor for a tadpole trike. One sketch, two boards:
// the LilyGO T-Display (ESP32) and the T-Display-S3, which also has touch.
//
// Three readings stacked like the vehicle seen from above -- front left on top,
// front right in the middle, rear at the bottom. The two front tiles carry a
// mark on their outer edge, which is what tells them apart now that they are no
// longer side by side.
//
// Protocol: docs/20-protocol.md section 3. Each sensor streams a 30-byte message
// at 2 Hz as two 15-byte notifications on characteristic 0xFFF4. The pressure
// float32 sits at offset 8 of the notification whose last byte is the tag 0x2E,
// so pressure never needs reassembly.
//
// Controls -- one grammar everywhere: a short press is a step, a long press on
// the right (A) goes in or confirms, a long press on the left (B) goes back out.
// Nothing repeats and nothing needs both buttons. Held for a moment, the two
// symbols show what holding on would do; let go and it does not happen.
//
// On the readings:
//   panel dark      either button brings it back, and does nothing else
//   B short         page through temperature, battery, serial
//   A short         back to the pressure
//   A held          open the setup menu
//
// The menu is one item per page: assignment, threshold front, threshold rear,
// learning. A steps forward, B back, A held goes in, B held leaves.
//
// Assignment is a list of the three wheels. A and B move the selection down and
// up; A held picks an entry up, after which A and B move the entry itself; A
// held puts it down again, and B held deletes it -- which has to be confirmed
// with the other button, so no sequence of presses on one button can destroy
// anything. Thirty seconds without input leaves.
//
// In a threshold screen: A is +0.1 bar, B is -0.1, and either held stores the
// pair and leaves. There is no discard -- you are looking at both values while
// you set them, so there is nothing to take back. Learning listens for a
// minute, or until three sensors have answered; if more than three answer, a
// chooser appears.
//
// On the S3, touch is added rather than substituted -- gloves defeat a
// capacitive panel. Tap a tile to page, hold one for 700 ms to select it, then
// tap another within six seconds to exchange the two.

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>
#include "FreeSansBold24pt7b.h"       // local copy: Arduino_GFX and Adafruit_GFX
                                      // both define GFXfont, so only one may be
                                      // on the include path

// --- hardware ------------------------------------------------------------ //
// One codebase, two boards. Everything below this block -- the BLE protocol, the
// link/position split, thresholds, learning, the button state machine -- is
// identical on both; only pins, panel wiring, layout and touch differ.
// These are the only definitions of the panel's size: nothing further down may
// restate them.
#if defined(ARDUINO_LILYGO_T_DISPLAY_S3)
  #define BOARD_NAME  "T-Display-S3"
  #define HAS_TOUCH   1
  #define PIN_A       14            // GPIO 0 is the strapping pin, so B goes there
  #define PIN_B       0
  #define PIN_BL      38
  #define PIN_PWR_ON  15            // the panel has no power at all without this
  #define SCR_W       170
  #define SCR_H       320
  #define TILE_H      106
  #define TOUCH_SDA   18
  #define TOUCH_SCL   17
  #define TOUCH_RST   21
  #define TOUCH_INT   16
  #define CST816_ADDR 0x15
  #define DISP_ROT    0
#else
  #define BOARD_NAME  "T-Display"
  #define HAS_TOUCH   0
  #define PIN_A       35            // input-only pad, pulled up on the board
  #define PIN_B       0
  #define PIN_BL      4
  #define SCR_W       135
  #define SCR_H       240
  #define TILE_H      80
  // Rotation 0, not 2: under Arduino_GFX the panel offsets have to be given
  // explicitly, and the pair that belongs to this orientation is 53/40. With the
  // other one a single column of memory outside the window stays visible as a
  // shimmering line down the long edge.
  #define DISP_ROT    0
#endif
#define TILE_W SCR_W

#if HAS_TOUCH
  #include <Wire.h>
  #include <esp_log.h>
#endif

#if defined(ARDUINO_LILYGO_T_DISPLAY_S3)
// 8-bit parallel (i8080). This panel is not on SPI, which is why Adafruit_ST7789
// cannot drive it and the whole display layer moved to Arduino_GFX.
Arduino_DataBus *bus = new Arduino_ESP32LCD8(7 /*DC*/, 6 /*CS*/, 8 /*WR*/, 9 /*RD*/,
                                             39, 40, 41, 42, 45, 46, 47, 48);
Arduino_GFX *gfx = new Arduino_ST7789(bus, 5 /*RST*/, DISP_ROT, true /*IPS*/,
                                      170, 320, 35, 0, 35, 0);
#else
Arduino_DataBus *bus = new Arduino_ESP32SPI(16 /*DC*/, 5 /*CS*/, 18 /*SCK*/,
                                            19 /*MOSI*/, GFX_NOT_DEFINED /*MISO*/);
Arduino_GFX *gfx = new Arduino_ST7789(bus, 23 /*RST*/, DISP_ROT, true /*IPS*/,
                                      135, 240, 52, 40, 53, 40);
#endif

Preferences prefs;

// Colours, named here rather than taken from a driver header, so the drawing code
// does not depend on which library is underneath.
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_CYAN   0x07FF
#define C_ORANGE 0xFD20
#define C_GREY   0x7BEF
#define C_DIM    0x4208

// Declared up here because the Arduino preprocessor inserts auto-generated
// prototypes above everything else -- a type used in a signature must precede
// them. What the events mean is in the control summary at the top of the file.
enum Ev { EV_NONE, EV_A_HINT, EV_B_HINT, EV_A_TAP, EV_B_TAP, EV_A_LONG, EV_B_LONG };
enum Mode { M_SHOW, M_MENU, M_ASSIGN, M_THR_F, M_THR_R, M_CONFIRM, M_LEARN, M_PICK };
// Learning sits last on purpose: it throws the whole assignment away before it
// starts, so it is the one page nobody should reach by overshooting.
enum MenuItem { MI_ASSIGN, MI_THR_F, MI_THR_R, MI_LEARN, MI_COUNT };
// Symbols rather than words: a caption small enough to fit two side by side on
// a 135-pixel panel is decoration, not information. Each carries its own
// colour, because the colour is part of what it means -- the bin is red
// wherever it appears, the tick green, everything else white. A caller free to
// choose would eventually paint a grey bin.
enum Sym  { SY_NONE, SY_BACK, SY_ENTER, SY_OK, SY_NO, SY_UP, SY_DOWN,
            SY_TRASH, SY_PLUS, SY_MINUS };
enum Icon { IC_ASSIGN, IC_LEARN, IC_THR_F, IC_THR_R };
enum Shown { S_BAR, S_BATT, S_TEMP, S_SERIAL };       // what the tiles show

// --- protocol ------------------------------------------------------------ //
#define TAG_PRESSURE 0x2E
#define TAG_TEMP     0x22
#define TAG_BATTERY  0x1E
static const NimBLEUUID SVC_UUID((uint16_t)0xFFF0);
static const NimBLEUUID CHR_UUID((uint16_t)0xFFF4);

// --- radio timings ------------------------------------------------------- //
#define STALE_MS   10000   // no notification for this long -> show a dash
#define DEAD_MS    15000   // connected but silent -> drop it, the slot is scarce
#define SEEN_MS    20000   // a sighting older than this no longer counts at all
// The scan runs in bounded sessions, not forever, and this is not a detail.
// setScanCallbacks(cb, false) turns the controller's duplicate filter ON, and
// that filter is only cleared when a scan is *started*. Inside one endless
// session each sensor is therefore reported exactly once, seenTab[].t never
// updates again, and the freshness test below becomes permanently
// unsatisfiable a few seconds in -- which is precisely why a sensor that
// dropped out never came back. Restarting the session is what makes being
// heard possible again.
#define SCAN_SESSION_MS 3000
#define FRESH_MS        10000   // a sighting this recent is worth connecting to

// --- limits and timings the rider can feel ------------------------------- //
#define LEARN_MS    60000UL   // listen this long before deciding who is who, or
                              // until three have answered, whichever comes first
#define SETTINGS_MS 30000UL   // wandering off mid-setting returns to the readings
#define PAGE_MS     180000UL  // an info page falls back to the pressure then
#define LIGHT_MS    60000UL   // and a press keeps the panel lit for this long
#define THR_MIN     1.0f      // the settable range of a threshold
#define THR_MAX     8.0f
// Our own threshold, because the sensor's is not reachable. The FXTH87 inside
// does raise an undervoltage flag, but on the SPI bus between it and the nRF
// (docs/20-protocol.md 1.1) -- over the radio only the percentage comes out, in
// the 0x1E record and in the SIG Battery Level characteristic. Twenty is a guess
// informed by one measurement: a sensor held in a connection fell from 100 % to
// 38 % in a day, so the last fifth is not a lot of riding.
#define BATT_WARN  20.0f      // below this the bar's remainder fills red

// --- slots --------------------------------------------------------------- //
enum Pos { VL = 0, VR = 1, HR = 2, NPOS = 3 };
static const char* POS_LABEL[NPOS]  = { "VL", "VR", "H" };
static const char* POS_SHORT[NPOS]  = { "LINKS", "RECHTS", "HINTEN" };
static const char* POS_KEY[NPOS]    = { "sVL", "sVR", "sH" };
static const char* ADDR_KEY[NPOS]   = { "aVL", "aVR", "aH" };
// The last tile is pinned to the bottom edge rather than stacked, so any
// leftover pixels fall between the tiles instead of below all three.
static const int   TILE_Y[NPOS]     = { 0, TILE_H, SCR_H - TILE_H };

static const uint8_t AVG_N = 8;       // samples averaged; see Link::ring below

// A Link is a connection to one sensor. A position is a wheel on the vehicle.
// Keeping them apart is what makes re-assignment instant: rotating permutes the
// position->link table and nothing is disconnected. (The earlier design stored
// the data per wheel, so every rotation had to tear down all three links --
// seconds of blank tiles for a change that is purely cosmetic.)
struct Link {
  String        serial;
  NimBLEClient* client     = nullptr;
  bool          subscribed = false;
  uint32_t      lastRx     = 0;
  uint32_t      nextTry    = 0;
  uint16_t      backoff    = 500;
  bool          haveData   = false;
  float         bar = NAN, tempC = NAN, batt = NAN;
  // The raw stream is noisy: in a measured 91 s capture the per-sample standard
  // deviation was 4.7 kPa and neighbouring samples differed by up to 25 kPa. Two
  // decimals of that is unreadable, and a single low outlier would raise a false
  // alarm. Eight samples at 2 Hz average over four seconds.
  float         ring[AVG_N];
  uint8_t       ringFill = 0, ringPos = 0;
  bool          alarmed  = false;      // latched, for threshold hysteresis
  float         baseBar  = NAN;        // reference pressure and the temperature
  float         baseTemp = NAN;        // it was taken at
  bool          leaking  = false;
  bool          known    = false;      // its attributes have been discovered once
  uint32_t      subAt    = 0;          // when the subscription started
  bool          logged   = false;      // negotiated interval reported once
  // The address is remembered so the scan can stay passive: matched against an
  // advertisement it identifies the sensor without asking for the name, which
  // the sensor would otherwise have to transmit on every single advertisement.
  // Connecting still waits for a fresh sighting -- see serviceLink().
  NimBLEAddress addr;
  bool          haveAddr = false;
};
Link conn[NPOS];
int  posLink[NPOS] = { 0, 1, 2 };     // posLink[VL] indexes conn[]

// --- state the screens and the buttons share ----------------------------- //
float    thrFront = 5.0f, thrRear = 5.0f;
Mode     mode = M_SHOW;
Mode     lastMode = M_THR_R;          // differs from mode, so the first draw clears
Shown    shown = S_BAR;               // short presses peek at the other values
bool     fakeMode = false;            // three invented sensors, radio off
uint32_t lastInput = 0;               // for the settings timeout
uint32_t lightUntil = 0;              // backlight held on until this moment
uint32_t shownUntil = 0;              // an info page falls back to pressure then

bool     uiDirty = true;
bool     showHelp = false;            // the two button labels, while one is held
// Bumped on every mode change. The per-screen caches compare against it, so a
// screen revisited with unchanged values still repaints after the clear instead
// of staying blank.
uint16_t drawEpoch = 0;

int      menuSel = 0;
// The assignment screen is a list with two stages: a selection that moves, and
// an entry that has been picked up and moves itself. One index and one flag are
// the whole of its state; the delete dialog reads them too, since that is the
// only place it can be reached from.
int      assignSel  = 0;
bool     assignHeld = false;

// Learning: the window, and what was chosen by hand while it stood open.
uint32_t learnUntil = 0;
String   pickSel[NPOS];
int      pickPos = 0, cand = 0;

// --- slow punctures ------------------------------------------------------ //
// Slow punctures are the case that falls between the other two: the threshold
// sits far below, and a change this gradual never shows up as a rate. So each
// sensor keeps a reference pressure, taken once it reads steadily, and the
// current value is compared against it over hours.
//
// Temperature is what makes the naive version useless. A tyre at 7 bar is 8.0
// bar absolute; warming it from 15 to 35 degC raises that to 8.57, which reads
// as 7.55 -- more than half a bar from sunshine alone, and the same again
// downwards as it cools in the evening. Any leak threshold below that would cry
// wolf every night. Since the sensor reports its own temperature we can divide
// it out, assuming constant volume and an ideal gas:
//
//     p_norm = (p + P_ATM) * (T_ref + 273.15) / (T + 273.15) - P_ATM
//
// What it cannot see is ambient pressure: 1000 m of altitude reads as 0.11 bar
// more. That appears as a rise, so it costs a re-reference rather than a false
// alarm -- and on the way back down, 0.11 bar of leak detection.
#define P_ATM    1.013f
#define LEAK_BAR 0.30f    // below the reference, compensated: a slow puncture
#define PUMP_BAR 0.10f    // above it: somebody pumped, so take a new reference

// No forward declarations anywhere in this sketch. The Arduino build writes a
// prototype for every function in every .ino itself and puts them all above the
// first one, so a function may be called from a file that is compiled earlier --
// which is what the fifty-odd declarations that used to stand here were for.
//
// The one thing that generation cannot do is declare a type. Every enum and
// struct used in a signature therefore has to stand above the point where those
// prototypes land, which is why they are all at the top of this file.
static Link& at(int pos) { return conn[posLink[pos]]; }

// --- what the scanner has seen ------------------------------------------- //
#define MAX_SEEN 10
struct Seen {
  char          name[24];
  NimBLEAddress addr;
  int           rssi;
  uint32_t      t;
  bool          used;
};
Seen seenTab[MAX_SEEN];
portMUX_TYPE seenMux = portMUX_INITIALIZER_UNLOCKED;


class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    std::string n = d->getName();
    if (n.rfind("AIRSPY", 0) == 0) {
      noteSeen(n.c_str(), d->getAddress(), d->getRSSI());
      return;
    }
    // A passive scan reports no name: the name sits in the scan response, and
    // asking for that costs the sensor an extra transmission on every single
    // advertisement. Once we know a sensor's address we do not need its name,
    // so match on the address and file the sighting under the serial we have.
    if (!n.empty()) return;
    for (int i = 0; i < NPOS; i++)
      if (conn[i].haveAddr && !conn[i].serial.isEmpty()
          && conn[i].addr == d->getAddress()) {
        noteSeen(conn[i].serial.c_str(), d->getAddress(), d->getRSSI());
        return;
      }
  }
};
static ScanCB scanCB;

// Tiles are composed in RAM and pushed in one transfer. Painting them straight
// onto the panel meant erasing and redrawing the digits several times a second,
// which reads as flicker -- most visibly against an inverted alarm background.
// All three tiles share one buffer, so its width must match theirs exactly:
// drawRGBBitmap assumes the buffer stride is the width it is handed. Every
// screen that updates in place borrows it too, which is why nothing else may
// be wider than the panel or taller than a tile.
//
// The geometry comes from the board block at the top of this file and nowhere
// else. It used to be repeated here as literals, which on the S3 quietly
// redefined the real 170x320 to 135x240 for every file that follows.
Arduino_Canvas *tileBuf = new Arduino_Canvas(TILE_W, TILE_H, nullptr);

struct TileState { char txt[12]; bool alarm, mark, leak, help; uint8_t batt, what; };
TileState prevTile[NPOS];
bool prevValid[NPOS] = { false, false, false };

// --- setup / loop -------------------------------------------------------- //
void setup() {
  Serial.begin(115200);

  // No deep sleep any more. This hangs on the vehicle's battery, where 20 mA is
  // nothing, and a warning device that cannot hear anything is worse than
  // useless. Powered means running; the plug is the off switch -- and pulling
  // it also lets the sensors' own cells sleep, which is what the parking
  // machinery was built for and never reliably achieved.
#if defined(ARDUINO_LILYGO_T_DISPLAY_S3)
  pinMode(PIN_A, INPUT_PULLUP);
#else
  pinMode(PIN_A, INPUT);
#endif
  pinMode(PIN_B, INPUT_PULLUP);

#if defined(ARDUINO_LILYGO_T_DISPLAY_S3)
  // Without this the panel has no supply at all and the screen simply stays dark.
  pinMode(PIN_PWR_ON, OUTPUT); digitalWrite(PIN_PWR_ON, HIGH);
#endif
  pinMode(PIN_BL, OUTPUT); digitalWrite(PIN_BL, HIGH);

  gfx->begin();
  gfx->fillScreen(C_BLACK);

  // The panel geometry is checked by looking for a stray column along the long
  // edge, not by a start-up pattern: a wrong offset leaves one column of memory
  // outside the window visible, which is unmistakable and costs no screen time.
  Serial.printf("panel reports %d x %d\n", gfx->width(), gfx->height());
  tileBuf->begin();
#if HAS_TOUCH
  touchBegin();
#endif

  // Measured: 115 mA while scanning, 53 mA once connected with the scan off, so
  // the radio was never the only thing worth looking at. 240 MHz buys nothing in
  // a program that spends its time waiting; BLE needs 80 as a floor.
  setCpuFrequencyMhz(80);
  Serial.printf("\n=== AIRSPY trike monitor (%s) @ %lu MHz ===\n",
                BOARD_NAME, (unsigned long)getCpuFrequencyMhz());
  loadConfig();
  // Held at start-up: work on the device without the vehicle anywhere near.
  fakeMode = (digitalRead(PIN_A) == LOW);

  // Put the layout up before the radio starts. Bringing BLE up costs the better
  // part of a second, and drawing afterwards meant the screen stayed black for
  // all of it -- the device looked dead while it was busy. Dashes are the honest
  // state anyway: no data yet, same as a sleeping sensor.
  drawUI();
  Serial.printf("display up at %lu ms\n", millis());

  if (fakeMode) {
    // A one-pixel border round the whole panel, while the banner is up anyway.
    // It answers a question nothing else can: if the top line is missing while
    // the other three are there, the panel's window is offset in memory; if all
    // four are drawn and one still cannot be seen, the case is over the glass.
    // Corners are the tell -- a clipped panel loses two of them, a bezel four.
    gfx->drawRect(0, 0, SCR_W, SCR_H, C_WHITE);
    drawCentred("DEMO", 0, SCR_H / 2 - 20, SCR_W, 30, NULL, 3, C_YELLOW);
    delay(2500);
    gfx->fillScreen(C_BLACK);
    fakeBegin();
    enterLearn();                      // start where a new device starts
    return;                            // the radio is never brought up at all
  }

  NimBLEDevice::init("");
  NimBLEDevice::setPower(3);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCB, false);
  scan->setActiveScan(true);
  scan->setMaxResults(0);              // we keep our own table; do not hoard theirs
  // 30 ms in every 100 is about a third of the time. The default is window ==
  // interval, which is a continuous receive that competes with the connection
  // events of every link already up.
  scan->setInterval(100);
  scan->setWindow(30);
  scan->start(0, false, true);
  lightUntil = millis() + LIGHT_MS;   // lit long enough for the values to arrive
  Serial.printf("radio up at %lu ms\n", millis());

  // Learning happens only on purpose, in a window opened from the setup menu --
  // there is no reset gesture to remember. Outside that window the set is
  // locked, so a stranger's sensor can never slip into a free slot while riding.
  // A device that has never been taught anything still starts in the window,
  // because there is nothing else it could usefully do.
  bool nothingStored = at(VL).serial.isEmpty() && at(VR).serial.isEmpty()
                    && at(HR).serial.isEmpty();
  if (nothingStored) enterLearn();
}

void loop() {
  handle(readButtons());
  if (fakeMode) fakeTick();
#if HAS_TOUCH
  serviceTouch();
#endif
  if (mode == M_LEARN) {
    String seen[MAX_SEEN];
    // No reason to sit out the rest of the window once the vehicle is complete.
    if (candidates(seen, MAX_SEEN) >= NPOS) closeLearnWindow();
    else if ((int32_t)(millis() - learnUntil) >= 0) closeLearnWindow();
  }
  if (mode != M_SHOW && mode != M_LEARN && mode != M_PICK
      && millis() - lastInput > SETTINGS_MS) settingsTimeout();

  static uint32_t lastService = 0;
  if (millis() - lastService > 250) {
    lastService = millis();
    if (!fakeMode) {                   // invented sensors need no servicing
      for (int i = 0; i < NPOS; i++) serviceLink(i);
      updateScanning();
    }
  }

  // Long enough to read something off, short enough that the pressure is back
  // before it matters that it went away.
  if (shown != S_BAR && (int32_t)(millis() - shownUntil) >= 0) { shown = S_BAR; uiDirty = true; }
  updateBacklight();

  static uint32_t lastDraw = 0;
  if (uiDirty || millis() - lastDraw > 60) {
    lastDraw = millis(); uiDirty = false;
    drawUI();
  }
  delay(10);
}
