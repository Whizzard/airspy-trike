# airspy_trike — handlebar monitor (ESP32)

One sketch, two boards, seven files that the build concatenates into one.
It compiles unchanged for either:

    arduino-cli compile --fqbn esp32:esp32:lilygo_t_display     airspy_trike
    arduino-cli compile --fqbn esp32:esp32:lilygo_t_display_s3  airspy_trike

Upload adds `-p <port>`; the classic T-Display needs `:UploadSpeed=115200`
appended to the FQBN, because 921600 drops the CH9102 mid-transfer.

The protocol it speaks is `docs/20-protocol.md` section 3: connect, enable
notifications on `0xFFF4`, read a `float32` at offset 8 of every notification
whose last byte is `0x2E`. No pairing, no handshake write.

## Layout

Three readings stacked like the vehicle seen from above: front left on top,
front right in the middle, rear at the bottom. The two front tiles carry a
vertical mark on their outer edge, which is what tells them apart once they are
no longer side by side.

## Controls

[`../docs/40-manual.md`](../docs/40-manual.md) shows every screen; this is the
summary.

One grammar, everywhere:

| | |
|---|---|
| short press | a step: page, move, change by 0.1 |
| long press right (A) | go in, choose, confirm |
| long press left (B) | go back out |

**A is forwards, B is back** — the menu travels right, a selection travels down,
a value goes up. Nothing repeats and nothing needs both buttons. Holding a
button for a moment shows what letting go would not do and holding on will; the
setting screens carry those two labels permanently, above their buttons.

On the readings: **B** pages temperature → battery → serial, **A** returns to
the pressure, **A held** opens the setup menu. There is nothing further out, so
a long left does nothing there. While the panel is dark either button simply
brings it back, and that press is spent doing so.

The menu is four pages, one sign each: assignment as a spanner, the two
thresholds as the vehicle from above with the wheels they guard lit, and
learning as a plus — learning last on purpose, because it throws the whole
assignment away before it starts, so it should not be the page you reach by
overshooting. There is no page for clearing. Learning begins by forgetting, so
clearing the assignment was learning without the useful half; and where erasing
is actually wanted — one sensor, not all three — the list does it.

**Assignment** is a list of the three wheels showing what actually tells the
sensors apart, all of it live: the engraved number to match against the valve,
the pressure so that squeezing a tyre identifies its wheel, and the temperature,
because a wheel in the sun reads differently from one in the shade. Three rows
and no heading — the colours are the readings screen's own, cyan for a serial,
white for a pressure, orange for a temperature.

It has two stages, told apart by the frame round the row. A white frame is a
selection, which **A** and **B** move down and up. **A held** picks the entry
up; the frame turns cyan and grows an arrow at each edge, over the button that
moves it, and now **A** and **B** move the sensor rather than the selection,
sliding it past the row it displaces — frame and all, because a frame that
stays behind marks the slot rather than the sensor. **A held** puts it down again. Thirty
seconds without a press leaves.

**B held** on a picked-up entry deletes that sensor. That gesture means "back
out" everywhere else, which is exactly why the confirmation is answered with
**A**: no sequence of presses on one button can throw anything away, because
saying yes needs the other hand. Deleting returns to the list, since having
removed one sensor you are probably about to sort out the other two.

Select-then-move replaced a 3-cycle paired with a transposition. Both reach all
six arrangements in at most three gestures, but one of them you have to work
out and the other you can see.

On the T-Display-S3 touch is added, not substituted — gloves defeat a capacitive
panel. Tap a tile to page; hold one for 700 ms to select it, then tap another
within six seconds to exchange the two, which beats the button gestures on the
readings screen since any pair can be swapped directly.

## Drawing

Two rules, and between them they are the whole of why nothing flickers.

`drawUI()` clears the panel once, when the mode changes. **No screen clears the
whole panel on an update** — it repaints only the parts of itself that moved,
which the per-screen caches already track.

And anything that changes while its screen stays up goes through a buffer:
composed in RAM, pushed in one transfer. Clearing a region and then drawing into
it leaves one frame of bare background, and a value repainting twice a second
turns that into a visible pulse. The tiles have always worked this way; the
menu band, the assignment rows, the threshold boxes and every updating line now
do too, all borrowing the tiles' buffer, which is why none of them may be wider
than the panel or taller than a tile.

The button labels are part of the bottom tile's transfer rather than drawn over
it, for the same reason: on top they were erased and redrawn every time that
tile repainted.

## Power

Powered means running. There is no deep sleep and no automatic disconnect: this
hangs on the vehicle's battery, where 20 mA is nothing, and a warning device
that cannot hear anything is worse than useless. The plug is the off switch —
and pulling it is also what lets the sensors' own cells sleep.

The panel goes dark after a minute without input and comes back on any button,
or by itself for an alarm or a slow puncture, which hold it lit until they are
resolved.

Measured on a T-Display over USB at 5 V, one sensor connected:

| State | Current |
|---|---|
| standby | 0.000 A (below the meter's resolution) |
| scanning | 0.115 A |
| connected, scan stopped | 0.053 A |
| …and at 80 MHz | 0.040 A |
| …and backlight off | 0.020 A |

Three things got it there, in order of what they were worth. The scan is 62 mA
on its own and never used to stop, so it now runs only while a sensor is
actually missing. The CPU gains 13 mA at 80 MHz — and nothing at all while
scanning, because the receiver swamps it. The backlight is the remaining half,
so the panel stays dark unless there is something to see.

On a 1000 mAh cell that is roughly 37 hours rather than six.

## What a connection costs the sensor

Holding a sensor in a BLE connection is expensive for *it*: measured, one fell
from 100 % to 38 % battery in a single day of being held. So once a link stands
the monitor asks for a 400–500 ms connection interval instead of NimBLE's
30–50 ms default, which is roughly a tenfold cut in how often the sensor has to
wake its radio.

An earlier version also disconnected after ten minutes of unchanged pressure.
That is gone: pressure is a poor stand-in for motion, and on a steady ride at
constant temperature it would have released all three sensors mid-journey.
Unplugging the monitor achieves the same thing without the failure mode.

## Layout of the sketch

The Arduino build concatenates every `.ino` in the folder into one translation
unit, so the split is for reading, not for linking — there are no headers, no
`extern` declarations and no forward declarations either. The build writes a
prototype for every function itself and puts them all above the first one, so
any file may call into any other regardless of the order they are compiled in.

| File | Holds |
|---|---|
| `airspy_trike.ino` | board switches and pins, protocol constants, the types and globals everything else shares, `setup()` and `loop()` |
| `ble.ino` | hearing the sensors, connecting, holding the links up |
| `config.ino` | what survives a power cut, and which sensor belongs to which wheel |
| `demo.ino` | three invented sensors, for working without the vehicle |
| `input.ino` | the two buttons, and what a gesture means wherever it is made |
| `screens.ino` | the menu and everything reached from it, in the order it offers them |
| `touch.ino` | the capacitive panel, which only the S3 has |
| `ui.ino` | the readings screen, and the primitives every screen draws with |

`airspy_trike.ino` must stay first: the build puts the file named after the
folder ahead of the rest, and the one thing prototype generation cannot do is
declare a type — so every enum and struct used in a signature has to be above
the point those prototypes land, which is the top of that file.

## Demo mode

Hold **A** while the device starts. A one-pixel border is drawn round the whole
panel first, which is the only way to tell a clipped panel from a tight case:
if a line is missing while the other three are there, the panel's window is
offset in memory; if all four are drawn and one still cannot be seen, something
is over the glass. The corners are the tell — a clipped panel loses two of them,
a bezel all four.

Then: Three sensors with random serials are
invented, the radio is never brought up, and the device begins where a new one
begins: nothing assigned, the learn window open. They turn up in the sightings
table a second and a half apart, so the counter climbs rather than jumps, and
the assignment follows from what was heard — the whole process, not a shortcut
to the readings. The window is a minute wide but closes the moment three
sensors have answered, so neither the demo nor a real vehicle sits it out.

The readings are not injected into the display. They are assembled into the same
15-byte notifications an AIRSPY sends and handed to `onNotify`, so the pairing
of the two halves, the averaging, the alarm hysteresis and the leak reference
with its temperature compensation are all exercised as they would be on the
vehicle. Pressures even land on the 1.671 kPa lattice the real sensor quantises
to, because code that only ever sees smooth numbers has not met the real thing.

Then faults arrive on their own, every minute or so on a random wheel: a slow
puncture at 0.15 bar a minute, a faster loss that falls through the alarm
threshold, or a cell going flat. Each sits at the bottom for half a minute and
is then pumped back up or replaced, which is how the real device clears those
warnings too.

Nothing written in demo mode reaches the flash. It begins by forgetting the
assignment, which is the point — but that must not cost the vehicle the one it
was carrying, so `savePositions()` and `saveThresholds()` return early while it
runs, and the demo's thresholds are its own.
