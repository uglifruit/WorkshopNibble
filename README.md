# NIBBLE

**Four buttons. Fifteen voltages. Ten notes.**

A program card for the [Music Thing Modular Workshop System
Computer](https://www.musicthing.co.uk/workshopsystem/) that turns the system's
**Four Voltages** module into a playable instrument.

Four Voltages is billed as a "minimum viable keyboard": four non-latching
buttons, one knob, four outputs from a resistor network. Press combinations and
you get different voltages — but they are arbitrary voltages, they change every
time you touch the knob, and nothing downstream knows what to do with them.

NIBBLE learns them. Patch one Four Voltages output into CV In 1, teach the card
which voltage each combination makes, and the ten combinations become ten notes
of a scale — or ten lo-fi drum sounds with a looper behind them.

Four buttons is four bits is one nibble. Hence the name.

---

## Two cards in one

| Boot | How | What |
|------|-----|------|
| **KEYS** | normal power-on | A ten-note keyboard. 1V/oct out, three envelope CVs, a gate. |
| **DRUMS** | hold the momentary switch at power-on | Ten parametric percussion voices, a four-bar event looper with lossless overdub, and a DJ filter. |

The LEDs say which one you got: KEYS lights the left column, DRUMS the right.

---

## Playing it: hold one, tap the others

This is the technique the card is built around, and it is worth learning first.

**Hold C. Tap A.** You get the AC sound. Let go of A — silence, no retrigger.
**Tap B.** You get BC. Let go — silence again. **Tap A.** AC again.

The held finger is a bank-select; the tapping finger plays. It repeats
indefinitely with no spurious hits, and it works exactly the same in KEYS.

That falls out of how the card solves its central problem. Four Voltages does
**not** return to a rest voltage when you let go — the output sits at the
last-pressed button's level. So releasing AB leaves the CV sitting at A's
voltage, which looks identical to genuinely pressing A. A naive design fires a
spurious note every time you lift a finger.

NIBBLE's rule: when a pair is released onto one of **its own two buttons**, that
one level is ignored. Anything else plays. So `AB → release → A` is silent, but
`AB → release → A → B` plays B, because the ghost was A, not B. And once the
voltage moves anywhere else, the ghost is forgotten — so you can come back to A
and it sounds.

---

## Calibrating: hold the switch for two seconds

The Four Voltages knob changes every voltage it produces, so the card cannot
guess and does not try to remember. Teach it whenever you move that knob.

**Hold the momentary switch down for 2 seconds** to start. Then, for each of ten
steps:

1. The LEDs show which buttons to press. **LEDs 0–3 are laid out exactly like
   the A B / C D buttons**, so the pattern *is* the instruction — no counting.
2. Hold that combination on Four Voltages.
3. **Tap the momentary switch** to capture it.
4. All six LEDs flash: got it. Next step.

The order is designed for your hand, not for the computer: the four singles,
then the pairs as **top row, bottom row, left column, right column, and the two
diagonals**.

| | |
|---|---|
| All six flash | captured |
| LEDs 4 and 5 flash once | the voltage was still moving — hold steady and tap again |
| LEDs 4 and 5 flash three times | captured, **but** this combination is too close to another one to tell apart |
| All six ramp up and fade | done |
| All six flash twice | aborted (hold for 2s again at any point) |

Learning is **not saved across power cycles**, deliberately. A stored
calibration would be a *wrong* calibration the moment the knob moved, and a card
that looks calibrated but plays the wrong notes is worse than one that admits it
knows nothing.

### If it says two combinations collide

Some knob positions genuinely squash two combinations into nearly the same
voltage, and no amount of software can separate them. Move the Four Voltages
knob a little and learn again. If one output is stubborn, **try one of the other
three** — they respond differently to the same buttons, and it costs nothing but
moving a cable.

Until you calibrate, the card runs an evenly-spaced guess so it still does
something. In KEYS, LED 5 tells you where you stand: **off** = never learned,
**slow blink** = learned but something collided, **solid** = learned cleanly.

---

## KEYS

The ten combinations play ten **degrees** of the selected scale — not ten
pitches. Combination → degree is fixed forever, so re-learning after moving the
knob gives you the same notes back. The four bare buttons are the bottom four
degrees; adding a second finger climbs.

### Panel

| Control | Does |
|---------|------|
| **Main** | Macro: length, filter and loudness together |
| **X** | How the macro is shared between those three |
| **Y** | Scale — twelve of them, ordered dark → bright (LEDs show a bar while you turn) |
| **Switch UP** | Fast glide between notes |
| **Switch MID** | Stepped pitch |
| **Switch tap** | Retrigger the current note |

### In / out

| Jack | |
|------|--|
| CV In 1 | Four Voltages output — the keyboard |
| CV In 2 | Transpose, 1V/oct, ±2 octaves (only when patched) |
| Pulse In 1 | Retrigger |
| CV Out 1 | **1V/oct pitch**, calibrated |
| CV Out 2 | LENGTH envelope |
| Audio Out 1 | FILTER envelope |
| Audio Out 2 | LOUDNESS envelope |
| Pulse Out 1 | Gate on every note |

The three envelopes are meant for driving an external envelope, filter and VCA —
patch them at Slopes and the Humpback filters and one macro knob shapes the
whole voice. Note the audio outputs are DC-coupled and carry CV happily, but
they are **not** calibrated the way the CV outputs are: fine for modulation, not
accurate enough for pitch.

The X knob morphs between three relationships: **LONG** (long decays, filter
mostly shut, always audible), **BALANCED** (all three follow the macro equally),
and **BRIGHT** (short, open, with the loudness squared so the top of the macro
pops as an accent).

### The scales

Twelve, on the Y knob, ordered **dark → bright** from fully anticlockwise to
fully clockwise — so the knob reads as a single mood axis rather than a list,
and the LED bar (more light = brighter) matches it.

| Y | Scale | Semitones | Top of the ten |
|---|-------|-----------|----------------|
| 1 | Phrygian | 0 1 3 5 7 8 10 | +15 (1¼ oct) |
| 2 | Hirajoshi | 0 2 3 7 8 | +20 (1⅔ oct) |
| 3 | Harmonic Minor | 0 2 3 5 7 8 11 | +15 (1¼ oct) |
| 4 | Natural Minor | 0 2 3 5 7 8 10 | +15 (1¼ oct) |
| 5 | Minor Pentatonic | 0 3 5 7 10 | +22 (1⅚ oct) |
| 6 | m7 Arpeggio | 0 3 7 10 | +27 (2¼ oct) |
| 7 | Dorian | 0 2 3 5 7 9 10 | +15 (1¼ oct) |
| 8 | Major Pentatonic | 0 2 4 7 9 | +21 (1¾ oct) |
| 9 | Ionian (Major) | 0 2 4 5 7 9 11 | +16 (1⅓ oct) |
| 10 | Maj7 Arpeggio | 0 4 7 11 | +28 (2⅓ oct) |
| 11 | Whole Tone | 0 2 4 6 8 10 | +18 (1½ oct) |
| 12 | Chromatic | all twelve | +9 (¾ oct) |

The right-hand column is worth noticing: because the ten combinations are ten
*degrees*, a four-note arpeggio spreads them over more than two octaves while
the chromatic scale packs them into three quarters of one. Choosing a scale
chooses the **range** as well as the flavour, and the difference is obvious the
moment you play it — the arpeggios are the ones to reach for when you want the
buttons to cover real ground.

Root is C2, and **CV In 2** transposes the lot.

---

## DRUMS

Hold the momentary switch at power-on. Same calibration, same ghost rule — but
now the combinations are a kit. Kick and snare sit on the bare A and B, because
the sounds you hit most should need the fewest fingers.

| | | | | |
|---|---|---|---|---|
| A **kick** | B **snare** | C closed hat | D open hat | AB rim |
| AC low tom | AD mid tom | BC high tom | BD clap | CD cowbell |

Everything is **synthesised, not sampled**: pitch-swept triangle bodies with
exponential decay and a noise mix per voice, in the manner of Wild Pebble. That
is why the Y knob can reshape the whole kit at once — the parameters are live
variables rather than fixed audio — and why the card needs no flash region, no
USB and no upload tool.

It also keeps the kit *playable* rather than fixed: one knob takes it from deep
and slow to tight and clicky without a resampler in the way.

### Panel

| Control | Does |
|---------|------|
| **Main** | DJ filter — low-pass left, bypass at centre, high-pass right |
| **X** | Tempo, 40–240 BPM |
| **Y** | Kit character: lower and longer ↔ higher and shorter |
| **Switch MID** | Play the loop |
| **Switch UP** | Record / overdub |
| **Switch UP, held 2s** | Erase the loop (only when you are not playing) |
| **Switch tap** | Retrigger the last sound |

### The looper

Four bars. It records **events, not audio**, which is what makes overdub
lossless — the twentieth pass sounds exactly like the first, because nothing is
ever re-recorded. It is also why the tempo knob **re-times** a pattern instead of
pitching it, and why filter-knob moves can be recorded alongside the hits.

Hits are quantised to 1/16 on playback; filter sweeps deliberately are not.
Punching in with the switch does not reset the loop position.

---

## Under the hood

- Everything is fixed-point integer. No floats anywhere in the audio path.
- Level detection settles for ~12 ms before it believes a voltage, with a
  Schmitt band so two close combinations cannot flicker between each other.
  Triples and the all-four combination are recognised as *not* being any learned
  level, so they do nothing rather than guessing at a neighbour.
- Control logic runs at 3 kHz inside the 48 kHz audio callback; voices,
  envelopes and the filter run at full rate.
- Single core. There is nothing here that needs a second one.
- ~4.6% of flash and ~8.3% of RAM.

## Building

Needs the [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.2.0.

```sh
cmake -B build -G Ninja
cmake --build build          # -> build/nibble.uf2
```

Hold BOOTSEL on the Computer, plug in USB, drag the `.uf2` across.

There are Python models of the trickiest logic in `tools/` — the ghost rule and
learn round-trip, the filter's stability, and the looper's event ordering. Run
them after touching the corresponding C++.

## Status

**v1.0.0 builds clean and has not yet been tested on hardware.** Everything is
verified against models and the compiler only. `docs/HARDWARE-TESTING.md` is the
running order for the first real session, and `docs/DEVLOG.md` records the
design decisions and the bugs the models caught along the way.

The open question is whether all ten combinations are reliably separable on a
real Four Voltages at a usable knob position. The card reports collisions during
calibration rather than guessing, so it will tell you.

## Credits

ComputerCard by **Chris Johnson** (Music Thing Modular), MIT.

The drum voices are generalised from **Wild Pebble** (Workshop System release
74); the scale tables are from **Lockstep** (release 89); the degree-indexing
approach is from **CA Sequencer** (release 19); the Schmitt-band quantiser idiom
is from **Goldfish** (release 11); and the state-variable filter's update
ordering is from **bends** (release 45). Thanks to all of them.

Built with Claude Code.

## Licence

CC-BY-4.0 — use it, fork it, sell it, just credit Andy Jenkinson (uglifruit).
`ComputerCard.h` is Chris Johnson's and keeps its own MIT licence.
