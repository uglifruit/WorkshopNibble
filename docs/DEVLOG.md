# NIBBLE devlog

## v1.0.0 — first build

### What the card is for

The Workshop System's **Four Voltages** is described by Music Thing as a
"minimum viable keyboard". On its own it is less than that: four non-latching
buttons into a resistor network, producing voltages that are arbitrary, that
every position of its knob changes, and that nothing downstream can interpret.
NIBBLE is the missing half — it learns the voltages and turns the combinations
into notes.

### The problem that shaped everything

Four Voltages **does not return to a rest voltage**. Let go of everything and
the output sits at the last-pressed button's level. So releasing A+B leaves the
CV exactly where pressing A alone would put it.

There is no signal-level way to tell those apart, which means "detect a level
change, play a note" fires a spurious note on every release. Every design that
starts from "quantise the incoming CV" runs straight into this.

The resolution — the **ghost rule** — is to suppress exactly one level at a
time: when a pair is released onto one of *its own two buttons*, that level is
ignored, and only until the voltage moves elsewhere. Everything else plays,
including a bare single that was part of the pair just released.

It is worth recording that this turned out to be a *feature*, not damage
control. Hold C, tap A, tap B: you get AC and BC, with silent releases, forever.
The held finger becomes a bank-select and the tapping finger plays. That falls
straight out of the rule with no special-casing, and it is now the technique the
card is documented around.

### Decisions worth keeping

**Degrees, not pitches.** The obvious design quantises the incoming voltage to
the nearest scale note. That is wrong here: the levels are resistor-divider
outputs whose spacing is a property of the network, not of music. Nearest-note
quantising would remap the whole keyboard every time the Four Voltages knob
moved, and would collapse several combinations onto the same note whenever two
levels landed inside a semitone. Indexing by degree means combination → degree
is fixed forever, and the voltage only ever decides *which combination*.

**No persistence.** Tempting, and wrong. The Four Voltages knob invalidates
every learned level, so a saved calibration would silently restore a *wrong*
one. A card that looks calibrated while playing the wrong notes is worse than
one that admits it knows nothing.

**Tap to confirm, rather than auto-capture.** The first design waited for the CV
to move by a threshold and then settle. That could not distinguish "the player
has not pressed yet" from "the player pressed something that reads nearly the
same as the last combination" — so two close combinations caused a *timeout* and
an aborted learn. With an explicit tap the card captures what it is told to,
flags the collision, and **completes**. Degrading to a warning beats stalling on
the one thing you cannot separate.

**Events, not audio, in the looper.** Overdub is lossless, tempo re-times rather
than pitches, and the whole four-bar loop costs 2KB instead of ~400KB.

**One core.** WorkshopBio needed a second one only for TinyUSB. There is no USB
here, so there was nothing to move — and launching a core you do not need adds a
boot-order hazard for free.

### Four bugs the models caught

There is no host C++ compiler on this machine, so the risky logic was written in
Python first and ported. That paid for itself four times over — each of these
would have been miserable to diagnose by ear on hardware.

**1. A one-pole slew that stalled, asymmetrically.** `v += (target - v) >> shift`
stops moving once the difference drops below `2^shift`. On a signed shift the
stall is *directional*: exact from above, ~`2^shift` short from below. A settled
reading therefore depended on which combination it was approached from — 17
units of error, over a quarter of the collision threshold, which put two
combinations on the wrong side of a learned boundary. Fixed with `slew_exact()`;
the level detector now round-trips all ninety approach directions with zero
error. The stalling version is kept, documented, for audio smoothing where it is
harmless.

**2. A soft clipper with 0.33× gain.** The input was normalised by `3*kLimit`
and the output scaled back by only `kLimit`. Every drum sound played a third too
quiet — plausible enough to have shipped unnoticed. Now unity below the knee,
asserted by test.

**3. A looper sorted by the wrong key.** Events were sorted by their raw
recorded tick, but quantisation moves an event's *fire* time, sometimes across
the loop boundary. The playback cursor walk assumes sorted-by-fire-time, so it
misfired. Sorting by `FireTick()` restores the invariant.

**4. A looper that double-fired.** The fire condition was "anything overdue"
(`when <= playHead`), which looks like the safe choice. It is not: an event
firing at tick 0 then matches at *every* tick until the cursor passes it. Exact
matching is correct, and safe because the tick increment is well under one tick
even at 240 BPM.

Two of the four (1 and 3) were caught by a test that does not check a mechanism
at all, but a *round trip*: learn ten levels, then replay all ten and assert each
classifies back to itself. Worth remembering as a pattern.

### Two tests that were wrong before the code was

The DJ filter's monotonicity check failed twice against a filter that was
correct:

- measured at 1 kHz, it saw the resonant peak legitimately swell and fall as the
  corner swept past. Asserting otherwise would have meant deleting the resonance
  that makes it sound like a DJ filter.
- measured at 50 Hz, it saw nothing, because 50 Hz is inside the passband at
  every knob position.
- and with a sparse frequency grid, the Q≈2.7 peak swept *between* the sample
  points, so the total wobbled by tens of percent from measurement artefact
  alone.

Broadband energy over a dense log grid is the honest measure. Worth a note
because "the test failed, so change the code" would have made the card worse
three times running.

### Where it stands

Builds clean at `-Wall -Wextra -Wdouble-promotion -Wfloat-conversion`: ~4.6% of
flash, ~8.3% of RAM. Nothing has touched hardware yet — everything above is
model-verified and compiler-verified only.

### Untested on hardware

In rough order of how likely they are to need attention:

1. **The tolerance constants in `levels.h`.** `kSettleTol`, `kMatchWindow` and
   `kCollisionMin` are derived from the expected divider spacing, not measured.
   They are grouped at the top of that file precisely because they are expected
   to move.
2. **Whether ten combinations are separable at all** on a real Four Voltages,
   and at which knob positions. This is the card's central gamble.
3. Drum voice tuning — the ten presets are plausible numbers, not auditioned
   ones.
4. Envelope decay ranges, and whether the macro's three anchor relationships
   feel distinct in a patch.
5. Loop feel: whether 1/16 quantisation is right, and whether hold-UP-to-erase
   is comfortable or alarming.

### Ideas deliberately left out

- External clock into Pulse In 2 for DRUMS (Pulse In 2 is unused and reserved).
- Variable loop length; four bars is currently fixed.
- Swing / humanise — the raw untouched tick is already stored for it.
- Anything on Pulse Out 2.
