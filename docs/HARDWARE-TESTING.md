# Hardware session — what to check, in order

Arranged so that each step's failure mode is distinguishable from the next
one's.

**Session 1 found:** calibration and triggering both good. Envelopes far too
short (a real bug — they capped at 43 ms regardless of the knob). LED 5 blinking
distractingly. Ghost notes shown on the LEDs instead of the held combo. Singles
triggering in DRUMS, which fights repeated hits. Overdub appearing to overwrite
hits.

**Session 2 found:** the root sat at 3 V, which read as an octave jump when
tuning oscillators together — and was silently clipping the top of wide scales.
LEDs 4 and 5 lit in KEYS for no useful reason. Wanted a click track.

All fixed; the list below is updated for v1.2.0.

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

**LEDs 4 and 5 should be dark** while playing in KEYS — they only have jobs
during calibration and in DRUMS.

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

Afterwards LED 5 should be **solid** — it now just means "running a real
calibration", with no blinking while you play. If any step collided, the
finishing fade has LEDs 4 and 5 flashing over the top; that is the one and only
report.

**Also worth testing now:** run a calibration with **nothing patched into
CV In 1**. It should FAIL — the two LED columns alternating rapidly for about a
second and a half — and keep whatever calibration was there before. If it
completes normally, the span check is too loose.

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

- **Check the root is 0 V.** Play the lowest combo with nothing in CV In 2; the
  oscillator should be at its own unmodulated pitch. This is the fix for the
  octave jump — no winding down to find the keyboard.
- Play the four singles: should be four ascending scale degrees.
- Play the pairs: should continue upward from there.
- Check an octave against a tuner. It should be within a couple of cents.
- **Sweep Y to an arpeggio scale and play the top combo** — that is the highest
  voltage the card can produce (~5.9 V with full transpose). It should still be
  in tune, not flat from clipping.
- Turn **Y** — the LEDs show a bar, and the scale should change audibly.
- **Switch UP** → notes should glide; **MID** → they should step.
- **Tap the switch** → the same note re-fires.

---

## 6. Envelopes and harmony

Scope or listen to **Audio Out 1** (filter env) and **Audio Out 2** (loudness
env). These were the ones that felt far too short in session 1.

- **Main fully CCW** → both should be short, tens of milliseconds.
- **Main fully CW** → the filter should run to roughly **3.5 seconds** and the
  loudness to roughly **5.5 seconds**. If they still top out around a twentieth
  of a second, `kEnvFrac` in `keys.h` is not doing its job.
- The peak no longer drops as you shorten them, so a short note should still be
  a LOUD note.
- **X** now splits filter against loudness: CCW the filter shuts well before the
  note fades (plucks), CW it outlasts the amplitude so the note blooms (pads).

**CV Out 2 is a harmony voice now**, not an envelope. Patch it to a second
oscillator, with Pulse Out 1 as a shared gate:

- Every note should be accompanied by one a third above, in key.
- Turn **Y** and the harmony should change flavour with the scale — major and
  minor thirds according to position.
- On **Chromatic** it should be a major third, not a whole tone.
- **Switch UP** should glide both voices together, not one under the other.

Practical patch: Audio Out 1 → Humpback FM, Audio Out 2 → an amplifier,
CV Out 1 and CV Out 2 → the two oscillators.

---

## 7. DRUMS — shift and tap

Power-cycle holding the switch. Audio Out 1 and 2 both carry the whole kit.

**A single button should now make NO sound at all.** That is the change: singles
are shifts. If a bare press triggers anything, the suppression in `FireCombo()`
is not working.

Six voices, reachable as pairs:

| Hold | tap A | tap B | tap C | tap D |
|------|-------|-------|-------|-------|
| **A** | — | kick | closed hat | clap |
| **B** | kick | — | open hat | rim |
| **C** | closed hat | open hat | — | snare |
| **D** | clap | rim | snare | — |

The thing to test hardest: **hold A and tap B repeatedly.** You should get a
clean run of kicks, as fast as you can tap, with no extra sounds between them.
That is the whole reason singles went silent.

- **Y** should shift the kit lower/longer ↔ higher/shorter.
- **Main** is the DJ filter: low-pass left, **bypass at centre**, high-pass
  right. Centre should be findable by feel and audibly do nothing.
- Nothing should clip harshly — the soft clipper is modelled but never heard.

---

## 8. The looper

- **Switch MID** = play, **UP** = record.
- Record a few hits; they should loop, quantised to 1/16.
- **Overdub many passes** — earlier hits must not degrade, and must not stop
  being recorded. Session 1 reported hits being overwritten; the cause was
  filter automation filling the shared buffer, now capped. **Sweep the Main
  knob for several passes and then play more hits** — those hits must still
  record.
- Move **Main** while recording; the sweep should play back with the pattern,
  and a second pass over the same spot should REPLACE the first rather than
  fight it.
- Turn **X** — the pattern should speed up or slow down, *not* change pitch.
- **Patch a clock into Pulse In 1** (one pulse per beat). The loop should follow
  it, X should stop having any effect, and it should hand back to the knob about
  three seconds after the clock stops. Locking on should not stutter the
  pattern.
- **Pulse Out 2 is the click** — one short blip per beat, in step with LED 4.
  Patch it at a click voice and record along to it. It should stay a blip at
  40 bpm, not a long gate.
- **Hold the switch 2 s** → calibration starts AND the loop is erased.

---

## What to bring back

In rough priority order:

1. Whether the **envelopes** are now long enough, and whether the DARK/BALANCED/
   BRIGHT sweep on X is a useful axis or just three flavours of the same thing.
2. Whether the **six-voice kit** is the right six, and whether the shift-and-tap
   layout falls under the hand. The mapping is a table in `drums.cpp` and is
   cheap to change.
3. Whether **repeated hits** (hold A, tap B fast) are genuinely clean.
4. Whether the **harmony** on CV Out 2 is musical, or whether a fifth would be
   better than a third.
5. Whether **overdub** now behaves under heavy knob movement.
6. Whether the **external clock** locks and releases cleanly.
7. Drum voice tuning — the presets are still plausible numbers rather than
   auditioned ones.
8. Whether the **tolerance constants** need moving — `kSettleTol`,
   `kMatchWindow`, `kCollisionMin` at the top of `levels.h`. Symptoms:
   - notes flickering between two neighbours → `kDeadband`/`kCollisionMin` too small
   - presses ignored → `kMatchWindow` too small, or `kSettleTicks` too long
   - notes firing while your finger is still landing → `kSettleTicks` too short
