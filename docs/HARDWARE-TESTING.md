# First hardware session — what to check, in order

Nothing in NIBBLE has touched a real Workshop System yet. Everything is
model-verified and compiler-verified only. This is the running order for the
first session, arranged so that each step's failure mode is distinguishable
from the next one's.

Flash `FLASHME/nibble.uf2` (hold BOOTSEL, plug in USB, drag it across).

---

## 0. Before anything else

Patch **one Four Voltages output → CV In 1**. That is the whole required patch.

Have a tuner or a known-good oscillator handy for step 5.

---

## 1. Does it boot at all?

Power on normally. Expect the **left column** (LEDs 0, 2, 4) lit for about a
second.

| What you see | What it means |
|---|---|
| Left column, steady | KEYS booted, CV outputs are factory-calibrated. Good. |
| Left column, **blinking** | Booted, but `CVOutsCalibrated()` is false — the 1V/oct output will not track. Not a NIBBLE bug; the card's factory calibration is missing. |
| Nothing at all | Suspect the build or the flash, not the logic. |

Then power-cycle **holding the momentary switch down**. Expect the **right
column** (1, 3, 5).

> If both boots give the same column, the boot latch is reading the switch too
> early — the exact bug both sibling cards shipped. Look at `kBootWindowSamples`
> in `nibble.h`.

---

## 2. Do the LEDs follow the buttons?

Still in KEYS, before calibrating. Press each Four Voltages button and watch
**LEDs 0–3**, which are laid out in the same 2×2 as A B / C D.

The card is running an evenly-spaced *guess* at this point, so the mapping will
probably be wrong — that is expected and not a failure. What matters is that
**something changes** when you press, and that it is stable rather than
flickering.

**LED 5 should be off**, meaning "never calibrated".

---

## 3. Calibration — the big one

Hold the momentary switch **2 seconds**. LEDs 0–3 should start showing you which
combination to press, with LED 4 lit dim (singles phase).

Ten steps, in this order:

| Step | Press | LEDs blinking |
|---|---|---|
| 1–4 | A, then B, then C, then D | one at a time |
| 5 | A+B | top row |
| 6 | C+D | bottom row |
| 7 | A+C | left column |
| 8 | B+D | right column |
| 9 | A+D | diagonal |
| 10 | B+C | anti-diagonal |

For each: hold the combination, **tap the momentary switch**.

| Feedback | Meaning | Do |
|---|---|---|
| All six flash | captured | continue |
| LEDs 4+5 flash **once** | tapped while the voltage was still moving | hold steady, tap again |
| LEDs 4+5 flash **three times** | captured, but too close to an earlier level | note which step; continue |
| All six ramp and fade | finished | — |

**Record which steps collide.** That is the single most valuable number from
this session — it tells us whether ten combinations are separable on real
hardware at all, which is the card's central gamble.

If several collide: move the Four Voltages knob and run it again. If a position
cannot be found where most steps are clean, **try one of the other three Four
Voltages outputs** before concluding anything — they respond differently to the
same buttons.

Afterwards LED 5 should be **solid** (clean) or **slow-blinking** (collisions).

---

## 4. The ghost rule

The behaviour the whole card is built around. Test it deliberately.

1. **Hold C. Tap A.** → a note fires (AC).
2. **Release A**, keeping C held → **silence**. No second note.
3. **Tap B** → a note fires (BC).
4. **Release B** → silence again.
5. Repeat 1–4 several times → it should be perfectly repeatable.

Then the keyboard case:

6. **Press A+B** → note fires.
7. **Release both** — the voltage falls to A or B → **silence**.
8. **Press just B** → **a note fires**. (The ghost was whichever one it fell to,
   not both.)

> Extra notes on release = the ghost rule is not arming.
> Missing notes in step 3 or 8 = it is arming too eagerly, or not clearing.

Also: **press three buttons at once.** Nothing should happen at all — no note,
and the previous note stays as it was.

---

## 5. Pitch

Patch **CV Out 1** to an oscillator's 1V/oct input, **Pulse Out 1** to an
envelope.

- Play the four singles: should be four ascending scale degrees.
- Play the pairs: should continue upward from there.
- Check an octave against a tuner. It should be within a couple of cents.
- Turn **Y** — the LEDs show a bar, and the scale should change audibly.
- **Switch UP** → notes should glide; **MID** → they should step.
- **Tap the switch** → the same note re-fires.

---

## 6. Envelopes

Scope or listen to **CV Out 2** (length), **Audio Out 1** (filter), **Audio
Out 2** (loudness).

- **Main** should move all three together.
- **X** should change how they are shared: fully CCW favours long decays,
  centre is even, fully CW is short and bright with the loudness popping at the
  top of the macro.

Practical patch: CV Out 2 → Slopes, Audio Out 1 → Humpback FM, Audio Out 2 →
a VCA.

---

## 7. DRUMS

Power-cycle holding the switch. Audio Out 1 and 2 both carry the whole kit.

- Ten sounds: A kick, B snare, C/D hats, then the pairs.
- **Y** should shift the whole kit lower/longer ↔ higher/shorter.
- **Main** is the DJ filter: low-pass left, **bypass at centre**, high-pass
  right. The centre should be findable by feel and audibly do nothing.
- Nothing should ever clip harshly or click — the soft clipper is modelled but
  never heard.

---

## 8. The looper

- **Switch MID** = play, **UP** = record.
- Record a few hits; they should loop, quantised to 1/16.
- **Overdub several passes** — the earlier hits must not degrade at all. That is
  the whole point of storing events rather than audio.
- Move **Main** while recording; the filter sweep should play back with the
  pattern.
- Turn **X** — the pattern should speed up or slow down, *not* change pitch.
- **Hold UP for 2s without playing** → all six LEDs flash, loop erased.
  (Playing anything during the hold should cancel the erase.)

---

## What to bring back

In rough priority order:

1. **Which calibration steps collided**, and at what Four Voltages knob
   position. Whether a clean position exists.
2. Whether the **ghost rule** behaves as described in step 4.
3. Whether the **tolerance constants** need moving — `kSettleTol`,
   `kMatchWindow`, `kCollisionMin` at the top of `levels.h`. Symptoms:
   - notes flickering between two neighbours → `kDeadband`/`kCollisionMin` too small
   - presses ignored → `kMatchWindow` too small, or `kSettleTicks` too long
   - notes firing while your finger is still landing → `kSettleTicks` too short
4. Whether **12 ms** of settle latency is playable or feels sluggish.
5. Drum voice tuning — the ten presets are plausible numbers, never auditioned.
6. Whether hold-UP-to-erase is comfortable or too easy to trigger by accident.
