// The capacitive panel, which only the T-Display-S3 has.
// Part of the airspy_trike sketch: the Arduino build concatenates every
// .ino in this folder, so the types and globals in airspy_trike.ino are
// visible here without a header.
//
// Touch is added to the buttons rather than replacing them -- gloves on a
// handlebar defeat a capacitive panel -- so every gesture here has a button
// equivalent in input.ino. The whole file compiles away on the other board.

#if HAS_TOUCH
// CST816 capacitive controller, driven directly: six registers from 0x01 are all
// this needs, which is cheaper than another library dependency.
//   0x01 gesture, 0x02 finger count, 0x03..0x06 X high/low, Y high/low
int selTile = -1;                     // tile picked for a swap, -1 = none
uint32_t selUntil = 0;

bool touchPresent = false;            // probed once; without this every poll of an
                                      // absent controller floods the log with NACKs

static void touchBegin() {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);  delay(20);
  digitalWrite(TOUCH_RST, HIGH); delay(100);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(100000);
  // The CST816 drops into standby and NACKs register reads until it is touched.
  // That is normal and already handled as "no finger", but the IDF driver logs an
  // error for every single attempt, which buries everything else.
  esp_log_level_set("i2c.master", ESP_LOG_NONE);

  // Scan rather than assume: the address tells us which controller is fitted, and
  // whether this is the touch variant of the board at all.
  Serial.print("I2C scan:");
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      found++;
      if (a == CST816_ADDR) touchPresent = true;
    }
  }
  if (!found) Serial.print(" nothing");
  Serial.printf("  -> touch %s\n", touchPresent ? "ready" : "DISABLED");
}

static bool touchRead(int16_t& x, int16_t& y) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)CST816_ADDR, (uint8_t)6) != 6) return false;
  uint8_t d[6];
  for (int i = 0; i < 6; i++) d[i] = Wire.read();
  if ((d[1] & 0x0F) == 0) return false;            // no finger on the glass
  int16_t rx = ((d[2] & 0x0F) << 8) | d[3];
  int16_t ry = ((d[4] & 0x0F) << 8) | d[5];
  // The touch coordinates must follow whatever rotation the panel is drawn at,
  // or finger and picture run against each other.
#if DISP_ROT == 2
  x = SCR_W - 1 - rx;
  y = SCR_H - 1 - ry;
#else
  x = rx;
  y = ry;
#endif
  return true;
}

static int tileAt(int16_t y) {
  for (int i = 0; i < NPOS; i++)
    if (y >= TILE_Y[i] && y < TILE_Y[i] + TILE_H) return i;
  return -1;
}

// Touch adds to the buttons rather than replacing them: gloves on a handlebar
// defeat a capacitive panel, so every gesture still has a button equivalent.
//   tap on a tile      -> cycle pressure / battery / temperature
//   hold on a tile     -> select it, then tap another tile to exchange the two
static void serviceTouch() {
  if (!touchPresent) return;
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 30) return;        // 33 Hz is plenty for a finger
  lastPoll = millis();

  static bool down = false, longDone = false;
  static uint32_t downAt = 0;
  static int downTile = -1;
  int16_t x, y;
  bool now = touchRead(x, y);

  if (selTile >= 0 && millis() > selUntil) { selTile = -1; uiDirty = true; }

  if (now && !down) {
    down = true; downAt = millis(); longDone = false; downTile = tileAt(y);
  } else if (now && down && !longDone && millis() - downAt > 700) {
    longDone = true;
    if (mode == M_SHOW && downTile >= 0) {
      selTile = downTile; selUntil = millis() + 6000;
      uiDirty = true;
    }
  } else if (!now && down) {
    down = false;
    if (longDone || downTile < 0 || mode != M_SHOW) return;
    if (selTile >= 0 && selTile != downTile) {
      swapTiles(selTile, downTile);
      selTile = -1;
    } else if (selTile == downTile) {
      selTile = -1; uiDirty = true;
    } else {
      shown = (shown == S_TEMP) ? S_BATT : (shown == S_BATT) ? S_SERIAL : S_TEMP;
      shownUntil = millis() + PAGE_MS;
      uiDirty = true;
    }
  }
}
#endif  // HAS_TOUCH
