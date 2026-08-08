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
of a scale — or a lo-fi drum kit with a looper behind it.

Four buttons is four bits is one nibble. Hence the name.

---

## Two cards in one

| Boot | How | What |
|------|-----|------|
| **KEYS** | normal power-on | A ten-note keyboard: pitch and harmony on 1V/oct, two envelope CVs, a gate. |
| **DRUMS** | hold the momentary switch at power-on | Twelve parametric percussion voices played shift-and-tap, a four-bar event looper with lossless overdub, a DJ filter, and a bassline. |

The LEDs say which one you got: KEYS lights the left column, DRUMS the right.

---

## Playing it: hold one, tap the others

This is the technique the card is built around, and it is worth learning first.

**Hold C. Tap A.** You get a closed hat. Let go of A — silence, no retrigger.
**Tap B.** A snare. Let go — silence again. **Tap A.** Closed hat again.

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

**Calibration starts on its own at power-up** — the learned levels are never
saved, so it is the first thing you would do anyway. You can also start one any
time by **holding the momentary switch for 2 seconds**, and abort with the same
hold. Then, for each of ten steps:

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
| Ramp and fade, with LEDs 4 + 5 flashing over it | done, but something collided |
| Columns alternating fast, ~1.5 s | **failed** — nothing usable came in |
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
something. In KEYS, **LED 5 solid** means it is running a real calibration;
**off** means it is still guessing.

### If nothing is patched in

Calibration **fails** rather than pretending. If all ten captures land on
essentially the same voltage — nothing in CV In 1, a dead cable — the two LED
columns alternate rapidly for a second and a half and the previous calibration
is kept. It is a deliberately different pattern from the gentle fade of success
or the double blink of an abort.

---

## KEYS

The ten combinations play ten **degrees** of the selected scale — not ten
pitches. Combination → degree is fixed forever, so re-learning after moving the
knob gives you the same notes back. The four bare buttons are the bottom four
degrees; adding a second finger climbs.

### Panel

| Control | Does |
|---------|------|
| **Main** | Macro: how long the note lasts |
| **X** | How that length is split between filter and loudness |
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
| CV Out 2 | **1V/oct harmony** — a third above, in the current scale |
| Audio Out 1 | FILTER envelope |
| Audio Out 2 | LOUDNESS envelope |
| Pulse Out 1 | Gate on every note |
| Pulse Out 2 | (unused in KEYS) |

The two envelopes are for driving an external filter and VCA — patch them at the
Humpback filters and an amplifier and one macro knob shapes the whole voice.
They run from about **25 ms to over five seconds**, so the macro covers clicks
through to long swells. Note the audio outputs are DC-coupled and carry CV
happily, but they are **not** calibrated the way the CV outputs are: fine for
modulation, not accurate enough for pitch.

The X knob morphs between three relationships: **DARK** (the filter closes well
before the note fades — plucks and muted things), **BALANCED** (both together),
and **BRIGHT** (the filter outlasts the amplitude, so the note blooms rather
than shutting down — pads and swells).

**CV Out 2 is a second voice**, not a modulation source: the same note a third
higher, picked from whichever scale the Y knob is on, so it is major or minor
according to where you are in the scale. Patch it to a second oscillator with
Pulse Out 1 as a shared gate and the ten buttons play two-part harmony.

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

**The root of the scale is 0 V**, so an oscillator sitting at its own zero is
already in tune with the card — no winding it down an octave to find the
keyboard. **CV In 2** transposes upward from there, up to two octaves.

---

## DRUMS

Hold the momentary switch at power-on. Same calibration, same ghost rule — but
the buttons work differently, and this is the important part:

> **A single button is a SHIFT, not a sound. Only pairs make a noise.**

Hold one button down and tap the others. Hold **C** and tap A, B or D for three
different sounds; hold **D** and tap A, B or C for three more. Keep tapping the
same one and it repeats cleanly, as fast as you like.

That is there because percussion is mostly *repeated hits on the same drum*, and
a keyboard reading of the buttons cannot do it: to play AC twice you have to pass
back through C, and if C were itself a sound then every repeat would be
interrupted by a spurious one. Silent singles make the four buttons into
bank-selects you can hold for as long as you want.

**Twelve** voices, because **A-held-tap-B is not the same as B-held-tap-A**.

Those two close the same switches and produce an identical voltage — press order
simply is not in the signal. What the card *can* see is the level the voltage
came *from*, which is the button that was already down. Latching that recovers
the ordering and doubles the kit for free.

| Hold | tap A | tap B | tap C | tap D |
|------|-------|-------|-------|-------|
| **A** | — | cowbell | hi-hat metallic | open hi-hat |
| **B** | crash | — | kick deep | snare snappy |
| **C** | closed hat | snare | — | kick |
| **D** | tom 1 "pew" | syn tom 2 | syn drum 3 | — |

**Hold C** is the workhorse: with a right thumb on C, A / B / D fall under the
fingers as closed hat, snare and kick — a whole beat without moving your hand.
**Hold D** is the tom row for fills. **A** and **B** carry the colour, including
the crash.

### The sounds

All synthesised: a pitched body, a noise component, and an exponential decay on
each. Nothing is sampled, which is why the **Y knob** can reshape the whole kit
at once — every number below is a live variable, not a recording.

| Sound | Pitch | Fall | Length | Level |
|-------|-------|------|--------|-------|
| kick | 62 → 45 Hz | 6 ms | 182 ms | 100% |
| kick deep | 50 → 38 Hz | 12 ms | 331 ms | 78% |
| snare | 190 → 150 Hz | 3 ms | 40 ms | 100% |
| snare snappy | 230 → 180 Hz | 3 ms | 86 ms | 90% |
| closed hat | 6000 Hz | — | 46 ms | 86% |
| hi-hat metallic | 7600 Hz | — | 23 ms | 78% |
| open hi-hat | 7600 Hz | — | 153 ms | 43% |
| crash | 5200 Hz | — | 704 ms | 10% |
| cowbell | 800 Hz | — | 178 ms | 90% |
| tom 1 "pew" | 420 → 90 Hz | 49 ms | 178 ms | 90% |
| syn tom 2 | 300 → 110 Hz | 97 ms | 166 ms | 70% |
| syn drum 3 | 360 → 120 Hz | 49 ms | 86 ms | 78% |

**Pitch** is where the body starts and, where it sweeps, where it lands.
**Fall** is how long that sweep takes — the exponential drop that makes the toms
read as Simmons-style rather than as short bass notes. **Length** is how long
the voice stays audible. **Level** is its weight in the mix, and it is not
cosmetic: peak amplitude stops being a proxy for loudness once decay times
differ by twenty-fold. The crash sits at 10% precisely *because* it is the
longest voice — at full scale it delivered thirteen times a kick's total energy
and buried the kit. It now lands at 0.73×, deliberately under the beat rather
than level with it.

The three cymbal voices — **hi-hat metallic**, **open hi-hat** and **crash** —
are *ring-modulated* against a second oscillator at a deliberately non-integer
ratio, which is what makes them clang rather than hiss. The plain **closed hat**
is not, so the two hats sit differently in a pattern.

**Y** shifts the whole kit together: anticlockwise is lower and longer,
clockwise higher and shorter, roughly half to double pitch and ±3 decay steps.
It applies when a voice is *struck*, so sweeping the knob never warps a sound
that is already ringing — **and its movements are recorded into the loop**,
alongside the filter. Two automation lanes, so a pattern can change its own kit
character as it goes.

Sixteen voices can ring at once, and when they are all busy the card steals the
**quietest** rather than the oldest — so a voice near the end of its decay goes
before one that has just started.

### Panel

| Control | Does |
|---------|------|
| **Main** | DJ filter — low-pass left, bypass at centre, high-pass right — **recorded into the loop** |
| **X** | Tempo, 40–240 BPM — ignored while an external clock is running |
| **Y** | Kit character: lower and longer ↔ higher and shorter — **recorded into the loop** |
| **Switch MID** | Play the loop |
| **Switch UP** | Record / overdub |
| **Switch tap** | Retrigger the last sound |
| **Switch held 2s** | Calibrate — **and erase the loop** |

### In / out

| Jack | |
|------|--|
| CV In 1 | Four Voltages output — the buttons |
| CV In 2 | (unused in DRUMS) |
| Pulse In 1 | **External clock**, one pulse per beat — overrides the X knob |
| CV Out 1 | **1V/oct bassline**, played by the single buttons |
| CV Out 2 | Gate, 5 V, on each live single press — an envelope for the bass |
| Audio Out 1 | Drum bus, after the DJ filter |
| Audio Out 2 | The same drum bus — either socket gives you the whole kit |
| Pulse Out 1 | Gate on every hit, from the buttons or from the loop |
| Pulse Out 2 | **Click track**, one blip per beat |

The click runs whenever the loop does, so there is something to record along to.
Patch it at a click voice, or just watch LED 4.

Both audio outputs carry the same mono bus rather than a stereo pair — patching
one socket should give you the whole kit, not half of it.

### A bassline, for free

The four single buttons make no drum sound, so their CV output was going spare.
They play **four bass notes on CV Out 1** instead — root, a tone below, the
fifth, and the octave — gated on Pulse Out 1 alongside the hits.

| Button | A | B | C | D |
|--------|---|---|---|---|
| Note | root | −2 | +5th | +octave |

**CV Out 2 gates it** — a 5 V blip on each shift press, so the bass voice can
have its own envelope without opening on every drum hit the way Pulse Out 1
does. Live presses only; a recorded pattern has no shift presses in it.

No combos, no scale, no quantiser. Once a pattern is looping, patch CV Out 1 at
an oscillator and put a simple line under it with the same four buttons. It also
falls out of the shift gesture for nothing: holding a button to reach the kit
holds its bass note too, so the root sustains while you play drums over it.

A bonus rather than the feature — but it costs one table and an output nothing
else was using.

### The looper

Four bars. It records **events, not audio**, which is what makes overdub
lossless — the twentieth pass sounds exactly like the first, because nothing is
ever re-recorded. It is also why the tempo knob **re-times** a pattern instead of
pitching it, and why filter-knob moves can be recorded alongside the hits.

The loop stores **which sound** — kick, snare, crash — not which buttons made
it. How a hit was played belongs to the performance, not the pattern, and it
means re-arranging the gesture map can never silently change what an old loop
plays back.

Hits are quantised to 1/16 on playback; filter sweeps deliberately are not.
Punching in with the switch does not reset the loop position.

**Patch a clock into Pulse In 1** and the loop follows it — one pulse per beat,
taking over from the X knob for as long as it keeps arriving, and handing back
about three seconds after it stops. It nudges into phase rather than snapping,
so locking to an external clock never stutters the pattern.

To **erase**, hold the switch for two seconds to re-enter calibration. That is
the erase gesture: every other control is spoken for while playing, and the four
singles in particular get held for long stretches as shifts, so anything
button-based would fire constantly. Recalibrating is also the one moment a stale
loop is guaranteed to be meaningless — the levels are about to change.

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
