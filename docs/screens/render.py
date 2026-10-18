#!/usr/bin/env python3
"""Render the monitor's screens to PNG, for the manual in docs/40-manual.md.

These are renders, not photographs. The device cannot be read back -- the
T-Display's panel is wired without MISO -- so the drawing is reproduced here
from the same fonts and the same geometry constants the sketch uses:

  * FreeSansBold24pt7b.h is read out of the sketch folder itself;
  * the 5x7 built-in face is read out of Adafruit_GFX's glcdfont.c;
  * text is placed by the same centring arithmetic as drawCentredOn(), which
    is why the fonts have to be real: everything is positioned from the glyph
    bounds, so a substitute face would move every label.

What that cannot catch is a change to the sketch that nobody mirrors here.
Re-run this after changing a screen:

    python3 docs/screens/render.py

Needs Pillow. Writes docs/screens/*.png at 3x, nearest-neighbour, so the
pixels stay pixels.
"""
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow:  python3 -m pip install pillow")

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
SCALE = 3

# --- the panel, exactly as airspy_trike.ino defines it ---------------------- #
SCR_W, SCR_H = 135, 240
TILE_W, TILE_H = SCR_W, 80
TILE_Y = (0, TILE_H, SCR_H - TILE_H)
NPOS = 3
VL, VR, HR = 0, 1, 2
POS_LABEL = ("VL", "VR", "H")
POS_SHORT = ("LINKS", "RECHTS", "HINTEN")

C_BLACK, C_WHITE, C_RED = 0x0000, 0xFFFF, 0xF800
C_GREEN, C_YELLOW, C_CYAN = 0x07E0, 0xFFE0, 0x07FF
C_ORANGE, C_GREY, C_DIM = 0xFD20, 0x7BEF, 0x4208

BATT_WARN = 20.0
BAND_Y, BAND_H = SCR_H * 15 // 100, SCR_H * 55 // 100
MI_COUNT = 4
ROW_Y0, ROW_DY, ROW_H = 8, 66, 62
THR_BOX_H = 72


def rgb(c):
    r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def cdiv(a, b):
    return int(a / b)          # C truncates toward zero; // floors


# --- fonts ------------------------------------------------------------------ #
def find_glcdfont():
    for p in Path.home().glob("Documents/Arduino/libraries/*/glcdfont.c"):
        return p
    for p in Path.home().glob("Library/Arduino15/libraries/*/src/**/glcdfont.c"):
        return p
    sys.exit("glcdfont.c not found -- is Adafruit_GFX or Arduino_GFX installed?")


def parse_bytes(text):
    return [int(m, 0) for m in re.findall(r"0[xX][0-9a-fA-F]+|\b\d+\b", text)]


class Classic:
    """The 5x7 built-in face: five column bytes per character, LSB at the top."""

    def __init__(self, path):
        body = path.read_text()
        body = body[body.index("{") + 1:body.rindex("}")]
        body = re.sub(r"//[^\n]*", "", body)
        self.data = parse_bytes(body)
        assert len(self.data) >= 256 * 5, len(self.data)

    def bounds(self, txt, size):
        return 0, 0, 6 * size * len(txt), 8 * size

    def draw(self, cv, txt, cx, cy, size, col):
        for ch in txt:
            o = ord(ch) * 5
            for col_i in range(5):
                bits = self.data[o + col_i]
                for row in range(8):
                    if bits & (1 << row):
                        cv.fill_rect(cx + col_i * size, cy + row * size,
                                     size, size, col)
            cx += 6 * size


class Glyph:
    __slots__ = ("off", "w", "h", "xa", "xo", "yo")

    def __init__(self, off, w, h, xa, xo, yo):
        self.off, self.w, self.h, self.xa, self.xo, self.yo = off, w, h, xa, xo, yo


class GFXFont:
    """An Adafruit GFXfont header: bitmaps, a glyph table, first/last/yAdvance.

    The cursor for this kind of font is a baseline, not a top-left corner, and
    every glyph carries its own offsets -- which is the whole reason the real
    file has to be parsed rather than approximated.
    """

    def __init__(self, path):
        text = re.sub(r"//[^\n]*", "", path.read_text())
        blocks = re.findall(r"\{(.*?)\}\s*;", text, re.S)
        self.bitmap = parse_bytes(blocks[0])
        self.glyphs = []
        for row in re.findall(r"\{([^{}]*)\}", blocks[1]):
            v = parse_signed(row)
            if len(v) >= 6:
                self.glyphs.append(Glyph(*v[:6]))
        tail = parse_bytes(blocks[-1])
        self.first, self.last, self.yadv = tail[-3:]
        assert len(self.glyphs) == self.last - self.first + 1, len(self.glyphs)

    def glyph(self, ch):
        o = ord(ch)
        return None if o < self.first or o > self.last else self.glyphs[o - self.first]

    def bounds(self, txt, size):
        minx = miny = 1 << 30
        maxx = maxy = -(1 << 30)
        x = 0
        for ch in txt:
            g = self.glyph(ch)
            if g is None:
                continue
            if g.w and g.h:
                x1, y1 = x + g.xo * size, g.yo * size
                minx, miny = min(minx, x1), min(miny, y1)
                maxx = max(maxx, x1 + g.w * size - 1)
                maxy = max(maxy, y1 + g.h * size - 1)
            x += g.xa * size
        if maxx < minx:
            return 0, 0, 0, 0
        return minx, miny, maxx - minx + 1, maxy - miny + 1

    def draw(self, cv, txt, cx, cy, size, col):
        for ch in txt:
            g = self.glyph(ch)
            if g is None:
                continue
            bit, byte = 0, 0
            for yy in range(g.h):
                for xx in range(g.w):
                    if bit % 8 == 0:
                        byte = self.bitmap[g.off + bit // 8]
                    if byte & (0x80 >> (bit % 8)):
                        cv.fill_rect(cx + (g.xo + xx) * size,
                                     cy + (g.yo + yy) * size, size, size, col)
                    bit += 1
            cx += g.xa * size


def parse_signed(text):
    return [int(m, 0) for m in re.findall(r"-?0[xX][0-9a-fA-F]+|-?\b\d+\b", text)]


# --- the drawing primitives, as Adafruit_GFX implements them ---------------- #
class Canvas:
    def __init__(self, w, h, bg=C_BLACK):
        self.w, self.h = w, h
        self.px = [[rgb(bg)] * w for _ in range(h)]

    def pixel(self, x, y, col):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = rgb(col)

    def fill_screen(self, col):
        self.px = [[rgb(col)] * self.w for _ in range(self.h)]

    def fill_rect(self, x, y, w, h, col):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.pixel(xx, yy, col)

    def draw_rect(self, x, y, w, h, col):
        self.fill_rect(x, y, w, 1, col)
        self.fill_rect(x, y + h - 1, w, 1, col)
        self.fill_rect(x, y, 1, h, col)
        self.fill_rect(x + w - 1, y, 1, h, col)

    def draw_line(self, x0, y0, x1, y1, col):
        steep = abs(y1 - y0) > abs(x1 - x0)
        if steep:
            x0, y0, x1, y1 = y0, x0, y1, x1
        if x0 > x1:
            x0, x1, y0, y1 = x1, x0, y1, y0
        dx, dy = x1 - x0, abs(y1 - y0)
        err = dx // 2
        ystep = 1 if y0 < y1 else -1
        y = y0
        for x in range(x0, x1 + 1):
            self.pixel(y, x, col) if steep else self.pixel(x, y, col)
            err -= dy
            if err < 0:
                y += ystep
                err += dx

    def thick_line(self, x0, y0, x1, y1, w, col):
        for o in range(-(w // 2), w // 2 + 1):
            self.draw_line(x0 + o, y0, x1 + o, y1, col)
            self.draw_line(x0, y0 + o, x1, y1 + o, col)

    def draw_circle(self, x0, y0, r, col):
        f, ddF_x, ddF_y, x, y = 1 - r, 1, -2 * r, 0, r
        for p in ((0, r), (0, -r), (r, 0), (-r, 0)):
            self.pixel(x0 + p[0], y0 + p[1], col)
        while x < y:
            if f >= 0:
                y -= 1
                ddF_y += 2
                f += ddF_y
            x += 1
            ddF_x += 2
            f += ddF_x
            for dx, dy in ((x, y), (-x, y), (x, -y), (-x, -y),
                           (y, x), (-y, x), (y, -x), (-y, -x)):
                self.pixel(x0 + dx, y0 + dy, col)

    def fill_circle(self, x0, y0, r, col):
        self.fill_rect(x0, y0 - r, 1, 2 * r + 1, col)
        f, ddF_x, ddF_y, x, y = 1 - r, 1, -2 * r, 0, r
        while x < y:
            if f >= 0:
                y -= 1
                ddF_y += 2
                f += ddF_y
            x += 1
            ddF_x += 2
            f += ddF_x
            self.fill_rect(x0 + x, y0 - y, 1, 2 * y + 1, col)
            self.fill_rect(x0 - x, y0 - y, 1, 2 * y + 1, col)
            self.fill_rect(x0 + y, y0 - x, 1, 2 * x + 1, col)
            self.fill_rect(x0 - y, y0 - x, 1, 2 * x + 1, col)

    def fill_triangle(self, x0, y0, x1, y1, x2, y2, col):
        pts = sorted(((y0, x0), (y1, x1), (y2, x2)))
        (y0, x0), (y1, x1), (y2, x2) = pts
        if y0 == y2:
            self.fill_rect(min(x0, x1, x2), y0, max(x0, x1, x2) - min(x0, x1, x2) + 1, 1, col)
            return
        for y in range(y0, y2 + 1):
            if y < y1:
                a = x0 + (x1 - x0) * (y - y0) / max(1, y1 - y0)
            else:
                a = x1 + (x2 - x1) * (y - y1) / max(1, y2 - y1)
            b = x0 + (x2 - x0) * (y - y0) / (y2 - y0)
            lo, hi = int(round(min(a, b))), int(round(max(a, b)))
            self.fill_rect(lo, y, hi - lo + 1, 1, col)

    # --- text, placed the way drawCentredOn() places it -------------------- #
    def text(self, txt, x, y, font, size, col):
        (CLASSIC if font is None else font).draw(self, txt, x, y, size, col)

    def centred(self, txt, x, y, w, h, font, size, col):
        f = CLASSIC if font is None else font
        x1, y1, tw, th = f.bounds(txt, size)
        f.draw(self, txt, x + cdiv(w - tw, 2) - x1, y + cdiv(h - th, 2) - y1, size, col)

    def blit(self, other, x, y, w=None, h=None):
        w = other.w if w is None else w
        h = other.h if h is None else h
        for yy in range(h):
            for xx in range(w):
                if 0 <= x + xx < self.w and 0 <= y + yy < self.h:
                    self.px[y + yy][x + xx] = other.px[yy][xx]

    def save(self, name):
        img = Image.new("RGB", (self.w, self.h))
        img.putdata([p for row in self.px for p in row])
        img = img.resize((self.w * SCALE, self.h * SCALE), Image.NEAREST)
        img.save(OUT / name)
        return name


CLASSIC = Classic(find_glcdfont())
BIG = GFXFont(ROOT / "airspy_trike" / "FreeSansBold24pt7b.h")


# --- button symbols, from drawSymOn() -------------------------------------- #
def sym(cv, cx, cy, r, s):
    col = C_RED if s == "trash" else C_GREEN if s == "ok" else C_WHITE
    if s == "back":
        cv.fill_triangle(cx + r, cy - r, cx + r, cy + r, cx - r, cy, col)
    elif s == "enter":
        cv.fill_triangle(cx - r, cy - r, cx - r, cy + r, cx + r, cy, col)
    elif s == "up":
        cv.fill_triangle(cx - r, cy + r, cx + r, cy + r, cx, cy - r, col)
    elif s == "down":
        cv.fill_triangle(cx - r, cy - r, cx + r, cy - r, cx, cy + r, col)
    elif s == "ok":
        cv.thick_line(cx - r, cy, cx - cdiv(r, 3), cy + r - 2, 4, col)
        cv.thick_line(cx - cdiv(r, 3), cy + r - 2, cx + r, cy - r, 4, col)
    elif s == "no":
        cv.thick_line(cx - r, cy - r, cx + r, cy + r, 4, col)
        cv.thick_line(cx + r, cy - r, cx - r, cy + r, 4, col)
    elif s == "plus":
        cv.fill_rect(cx - r, cy - 2, 2 * r + 1, 5, col)
        cv.fill_rect(cx - 2, cy - r, 5, 2 * r + 1, col)
    elif s == "minus":
        cv.fill_rect(cx - r, cy - 2, 2 * r + 1, 5, col)
    elif s == "trash":
        cv.fill_rect(cx - 3, cy - r - 2, 6, 3, col)
        cv.fill_rect(cx - r, cy - r + 1, 2 * r, 3, col)
        cv.draw_rect(cx - r + 2, cy - r + 6, 2 * r - 4, 2 * r - 6, col)


def help_row(cv, left, right):
    y = SCR_H - 16
    cv.fill_rect(0, y - 14, SCR_W, 28, C_BLACK)
    sym(cv, 18, y, 10, left)
    sym(cv, SCR_W - 18, y, 10, right)


# --- the readings screen, from drawTile() / tileText() --------------------- #
class Sensor:
    def __init__(self, serial, bar, temp, batt, live=True, leaking=False):
        self.serial, self.bar, self.temp = serial, bar, temp
        self.batt, self.live, self.leaking = batt, live, leaking


def tile_text(s, shown):
    as_serial = shown == "serial" or (not s.live and shown != "bar")
    if not s.serial:
        return "", "", False
    if as_serial:
        return s.serial.split("-")[-1], "", True
    if not s.live:
        return "-", "", False
    if shown == "batt":
        return "%.0f" % s.batt, "%", False
    if shown == "temp":
        return "%.1f" % s.temp, "C", False
    return "%.2f" % s.bar, "", False


def value_unit(buf, val, unit, col):
    vx, vy, vw, vh = BIG.bounds(val, 1)
    uw = uh = 0
    if unit:
        _, _, uw, uh = CLASSIC.bounds(unit, 2)
    gap = 5 if unit else 0
    x = cdiv(TILE_W - (vw + gap + uw), 2)
    y = cdiv(TILE_H - vh, 2)
    BIG.draw(buf, val, x - vx, y - vy, 1, col)
    if unit:
        CLASSIC.draw(buf, unit, x + vw + gap, y + vh - uh, 2, col)


def tile(cv, pos, s, shown, thr, hint=False):
    alarm = s.live and s.bar < thr
    inverted = alarm and shown == "bar"
    leak = s.live and s.leaking
    txt, unit, as_serial = tile_text(s, shown)

    batt_pct = 255
    if s.live and s.batt is not None:
        batt_pct = max(0, min(100, int(s.batt)))
    batt_low = batt_pct != 255 and batt_pct <= int(BATT_WARN)

    if shown == "batt":
        fg = C_RED if batt_low else C_GREEN
    elif shown == "temp":
        fg = C_ORANGE
    elif shown == "serial":
        fg = C_CYAN
    else:
        fg = C_RED if alarm else (C_YELLOW if leak else C_WHITE)
    if as_serial:
        fg = C_CYAN
    if inverted:
        fg = C_BLACK
    if not s.live:
        fg = C_DIM if as_serial else C_WHITE

    buf = Canvas(TILE_W, TILE_H, C_RED if inverted else C_BLACK)
    if batt_pct != 255:
        avail = TILE_W - 20
        w = max(2, cdiv(avail * batt_pct, 100))
        if inverted:
            buf.fill_rect(10, TILE_H - 10, w, 5, C_BLACK)
        else:
            buf.fill_rect(10, TILE_H - 10, w, 5, C_GREEN)
            if batt_low:
                buf.fill_rect(10 + w, TILE_H - 10, avail - w, 5, C_RED)
    if pos in (VL, VR):
        buf.fill_rect(0 if pos == VL else TILE_W - 7, cdiv(TILE_H - 56, 2), 7, 56,
                      C_BLACK if inverted else C_WHITE)
    if txt:
        if as_serial:
            buf.centred(txt, 0, 0, TILE_W, TILE_H, None, 4, fg)
        else:
            value_unit(buf, txt, unit, fg)
    if hint:
        sym(buf, TILE_W - 18, TILE_H - 16, 10, "enter")
    cv.blit(buf, 0, TILE_Y[pos])


def readings(sensors, shown, thr, hint=False):
    cv = Canvas(SCR_W, SCR_H)
    for p in range(NPOS):
        tile(cv, p, sensors[p], shown, thr[p], hint and p == NPOS - 1)
    return cv


# --- the setup screens, from screens.ino ----------------------------------- #
def icon(cv, cx, cy, r, which, col=C_CYAN):
    if which == "assign":                               # a spanner
        d, w = cdiv(r * 3, 5), cdiv(r, 3)
        x0, y0, x1, y1 = cx - d, cy + d, cx + d, cy - d
        cv.thick_line(x0, y0, x1, y1, w, col)
        cv.fill_circle(x0, y0, w, col)
        cv.fill_circle(x0, y0, cdiv(w, 2), C_BLACK)
        cv.fill_circle(x1, y1, w, col)
        cv.fill_triangle(x1 - cdiv(w, 4), y1 + cdiv(w, 4),
                         x1 + cdiv(w * 14, 10), y1 - cdiv(w * 6, 10),
                         x1 + cdiv(w * 6, 10), y1 - cdiv(w * 14, 10), C_BLACK)
    elif which == "learn":                              # a plus
        w = 13
        cv.fill_rect(cx - r, cy - cdiv(w, 2), 2 * r, w, col)
        cv.fill_rect(cx - cdiv(w, 2), cy - r, w, 2 * r, col)
    else:                                               # the vehicle from above
        front = which == "thr_f"
        f, b = (col, C_DIM) if front else (C_DIM, col)
        w, h = cdiv(r, 3), cdiv(r * 2, 3)
        top, bot = cy - cdiv(r, 2) - cdiv(h, 2), cy + cdiv(r, 2) - cdiv(h, 2)
        cv.fill_rect(cx - cdiv(r * 3, 5) - cdiv(w, 2), top, w, h, f)
        cv.fill_rect(cx + cdiv(r * 3, 5) - cdiv(w, 2), top, w, h, f)
        cv.fill_rect(cx - cdiv(w, 2), bot, w, h, b)


MENU_NAME = ("Zuordnung", "Schwelle", "Schwelle", "Anlernen")
MENU_ICON = ("assign", "thr_f", "thr_r", "learn")


def menu(sel):
    cv = Canvas(SCR_W, SCR_H)
    band = Canvas(SCR_W, BAND_H)
    icon(band, SCR_W // 2, cdiv(BAND_H * 30, 100), cdiv(SCR_H * 15, 100), MENU_ICON[sel])
    band.centred(MENU_NAME[sel], 0, cdiv(BAND_H * 62, 100), SCR_W, 20, None, 2, C_WHITE)
    cv.blit(band, 0, BAND_Y)
    dy, gap = cdiv(SCR_H * 78, 100), 14
    x0 = SCR_W // 2 - cdiv((MI_COUNT - 1) * gap, 2)
    for i in range(MI_COUNT):
        cv.fill_circle(x0 + i * gap, dy, 4 if i == sel else 3,
                       C_CYAN if i == sel else C_GREY)
    help_row(cv, "back", "enter")
    return cv


def assign(sensors, sel, held):
    cv = Canvas(SCR_W, SCR_H)
    for p in range(NPOS):
        s = sensors[p]
        buf = Canvas(SCR_W, ROW_H)
        buf.centred(POS_LABEL[p], 8, 5, 16, 8, None, 1, C_GREY)
        if p == sel:
            fr = C_CYAN if held else C_WHITE
            for k in range(3):
                buf.draw_rect(k, k, SCR_W - 2 * k, ROW_H - 2 * k, fr)
            if held:
                sym(buf, 11, 16, 6, "up")
                sym(buf, SCR_W - 11, 16, 6, "down")
        tail = s.serial.split("-")[-1] if s.serial else "--"
        buf.centred(tail, 0, 3, SCR_W, 26, None, 3, C_CYAN if s.serial else C_DIM)
        if s.live:
            pb, tb = "%.2f" % s.bar, "%.0f" % s.temp
            pw, tw = len(pb) * 18, len(tb) * 12
            x = cdiv(SCR_W - (pw + 9 + tw), 2)
            CLASSIC.draw(buf, pb, x, 32, 3, C_WHITE)
            CLASSIC.draw(buf, tb, x + pw + 9, 32 + 8, 2, C_ORANGE)
        else:
            buf.centred("-", 0, 32, SCR_W, 26, None, 3, C_DIM)
        cv.blit(buf, 0, ROW_Y0 + p * ROW_DY)
    help_row(cv, "trash" if held else "back", "ok" if held else "enter")
    return cv


def confirm(s, pos):
    cv = Canvas(SCR_W, SCR_H)
    cv.centred("LOESCHEN?", 0, 54, SCR_W, 20, None, 2, C_RED)
    cv.centred(s.serial.split("-")[-1], 0, 88, SCR_W, 26, None, 3, C_WHITE)
    cv.centred(POS_SHORT[pos], 0, 126, SCR_W, 18, None, 2, C_GREY)
    help_row(cv, "no", "ok")
    return cv


def thresholds(front, thr_f, thr_r):
    cv = Canvas(SCR_W, SCR_H)
    cv.centred("SCHWELLE", 0, 16, SCR_W, 16, None, 2, C_CYAN)
    for i in range(2):
        active = (i == 0) == front
        buf = Canvas(SCR_W, THR_BOX_H)
        if active:
            buf.draw_rect(2, 0, SCR_W - 4, THR_BOX_H, C_CYAN)
        buf.centred("%.1f" % (thr_f if i == 0 else thr_r), 0, 10, SCR_W, 52,
                    BIG, 1, C_YELLOW if active else C_DIM)
        cv.blit(buf, 0, 48 + i * 80)
    help_row(cv, "minus", "plus")
    return cv


def learn(n, rest):
    cv = Canvas(SCR_W, SCR_H)
    cv.centred("ANLERNEN", 0, 22, SCR_W, 20, None, 2, C_CYAN)
    cv.centred(str(n), 0, 70, SCR_W, 56, BIG, 1, C_GREEN if n else C_YELLOW)
    cv.centred("Sensor" if n == 1 else "Sensoren", 0, 130, SCR_W, 16, None, 2, C_GREY)
    cv.centred("%d s" % rest, 0, 180, SCR_W, 20, None, 2, C_WHITE)
    help_row(cv, "no", "ok")
    return cv


# --- what the manual shows -------------------------------------------------- #
def main():
    ok = [Sensor("AIRSPY-26644", 6.85, 21, 92),
          Sensor("AIRSPY-25680", 6.90, 22, 78),
          Sensor("AIRSPY-14203", 5.12, 24, 14)]
    low = [ok[0], ok[1], Sensor("AIRSPY-14203", 4.62, 24, 14)]
    leaky = [ok[0], ok[1], Sensor("AIRSPY-14203", 5.12, 24, 14, leaking=True)]
    gone = [ok[0], Sensor("AIRSPY-25680", 0, 0, None, live=False), ok[2]]
    waiting = [Sensor(s.serial, 0, 0, None, live=False) for s in ok]
    thr = (6.0, 6.0, 5.0)

    made = [
        readings(ok, "bar", thr).save("readings-pressure.png"),
        readings(low, "bar", thr).save("readings-alarm.png"),
        readings(leaky, "bar", thr).save("readings-leak.png"),
        readings(gone, "bar", thr).save("readings-offline.png"),
        readings(waiting, "bar", thr).save("readings-waiting.png"),
        readings(ok, "temp", thr).save("readings-temperature.png"),
        readings(ok, "batt", thr).save("readings-battery.png"),
        readings(ok, "serial", thr).save("readings-serial.png"),
        readings(ok, "bar", thr, hint=True).save("readings-hint.png"),
        menu(0).save("menu-assignment.png"),
        menu(1).save("menu-threshold-front.png"),
        menu(2).save("menu-threshold-rear.png"),
        menu(3).save("menu-learn.png"),
        assign(ok, 0, False).save("assign-browse.png"),
        assign(ok, 1, True).save("assign-held.png"),
        confirm(ok[1], VR).save("assign-delete.png"),
        thresholds(True, 6.0, 5.0).save("threshold-front.png"),
        thresholds(False, 6.0, 5.0).save("threshold-rear.png"),
        learn(2, 43).save("learn.png"),
    ]
    print("\n".join(made))


if __name__ == "__main__":
    main()
