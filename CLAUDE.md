# NIBBLE — working notes for Claude Code

A program card for the **Music Thing Modular Workshop System Computer** (RP2040),
built on the header-only **ComputerCard** library. Sibling project to
`../WorkshopBio` and `../WorkshopZX` — reuse their conventions and structure
where they fit.

NIBBLE reads **one output of the Workshop System's Four Voltages module** on
CV In 1 and turns its button combinations into notes (KEYS) or drum hits
(DRUMS).

## Build

Toolchain comes from the Pico VS Code extension install at `~/.pico-sdk/`.
`CMakeLists.txt` includes `~/.pico-sdk/cmake/pico-vscode.cmake`, which pins
SDK 2.2.0 / GCC 14_2_Rel1 / picotool 2.2.0-a4.

From PowerShell:

```powershell
$env:PICO_SDK_PATH = "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
$env:PATH = "$env:USERPROFILE\.pico-sdk\cmake\v3.31.5\bin;$env:USERPROFILE\.pico-sdk\ninja\v1.12.1;$env:USERPROFILE\.pico-sdk\toolchain\14_2_Rel1\bin;$env:PATH"
cmake -B build -G Ninja
cmake --build build
```

Output: `build/nibble.uf2`. Copy to `FLASHME/` for flashing (git-ignored).
`cmake`/`ninja` are **not** on the default PATH — always set it as above.

## Hard rules

- `ProcessSample()` runs at **48 kHz** on core 0, inside a DMA interrupt.
  Allocation-free, no `malloc`, no blocking, no `float` in the hot path —
  fixed-point only.
- Audio/CV I/O is signed 12-bit (`-2048..2047`). `KnobVal()` is unsigned 12-bit
  (`0..4095`).
- **Never** do hardware setup in the `ComputerCard` constructor — it wedges the
  chip. Setup goes in `main()`.
- `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` is required for the Workshop
  Computer's crystal. Don't remove it.
- The switch reads **Down for the first few milliseconds of every boot**
  (ComputerCard derives it from `knobs[3]`, off a ~60 Hz filter starting at
  zero, and zero decodes as Down). Latch the alt-boot from **one** reading after
  a settle window — never "Down seen at any point". Both sibling cards shipped
  that bug.
- `CVOutMillivolts()` / `CVOutMIDINote()` are **flash-resident**. Cache the last
  value and only call them on a change, or they put XIP reads in the hot loop.
- Build clean: `-Wall -Wextra -Wdouble-promotion -Wfloat-conversion` are on.
  Watch `--print-memory-usage` at link time. Currently ~4.6% flash, ~8.3% RAM.

## Why there is no core 1

Deliberate, not an omission. The whole per-sample load — one SVF, a few decaying
drum voices, the envelope and CV stage — is a few hundred cycles against a
budget of 4000 at 192 MHz.

WorkshopBio needed a second core only because TinyUSB's `tud_task()` measured at
~36000 cycles, fourteen times the entire budget. NIBBLE speaks no USB, so there
is nothing to move over, and launching a core it doesn't need adds a real
boot-order hazard for zero benefit. **Do not cargo-cult the multicore setup
across from the sibling cards.**

## Why nothing is written to flash

The learned levels are **RAM only, by design**. The Four Voltages knob moves
every one of them — it is a resistor network, and the knob is documented as
changing all four outputs in unpredictable directions. A saved calibration would
silently restore a *wrong* one on the next power-up, which is worse than having
none: the card would look calibrated and play the wrong notes.

`hardware_flash` is still linked, but only because `ComputerCard.h` itself needs
it to read the factory CV-output calibration from EEPROM. That is not our
storage.

## The ghost rule

The one piece of logic that cannot be re-derived from reading the code around
it. Full treatment at the top of `levels.h`; the short version:

Four Voltages does **not** return to a rest voltage when you let go — the output
sits at the last-pressed button's level. Releasing AB leaves the CV at A's
voltage, which is indistinguishable from pressing A, so a naive detector fires a
spurious note on every release.

So: when a **pair** is active and the level settles on one of **that pair's own
two members**, that level is a *ghost* — suppressed, no retrigger. Anything else
triggers, including a bare single that was part of the pair just released. Only
one level is suppressed at a time, and it is cleared the instant the CV moves
elsewhere. In `Step()`, `ghost_ = kComboNone` executing **first and
unconditionally** is what implements that; the ordering is the rule.

The payoff, which falls out with no special-casing: hold C, tap A → AC sounds
and the release back to C is silent. Tap B → BC. The held finger is a
bank-select and the tapping finger plays. That is the intended DRUMS technique.

## Architecture

Control tick at **3 kHz** (`kCtrlDiv = 16`), audio at 48 kHz. Higher than
WorkshopBio's 1.5 kHz because the settle detector's granularity is one control
tick, and for a keyboard timing *jitter* is more audible than latency.

**The divider buys throughput, not slack.** `ControlTick()` runs INLINE inside
`ProcessSample()`, which is a DMA interrupt handler — on the sample where it
fires, everything must still finish within that one 20.83 µs slot. Never quote
an "amortised cycles/sample" figure as headroom; the worst single sample is what
decides whether audio glitches.

Beware `slew()` in `fastmath.h`: the plain shift **stalls** short of its target,
asymmetrically. That is fine for audio smoothing and fatal anywhere a value is
later compared against a threshold. Use `slew_exact()` there — it cost 17 units
of direction-dependent error in the level detector before it existed.

## Layout

| Path | Purpose |
|------|---------|
| `main.cpp` | Card entry, `ProcessSample()`, boot latch, switch gestures, learn machine, LEDs, output routing |
| `nibble.h` | Shared constants, combo indices, rates, LED helpers |
| `levels.h/.cpp` | Level detection, settle/match, **the ghost rule** |
| `scales.h` | 12 scale tables + `QuantizeNote()` |
| `keys.h/.cpp` | KEYS: macro distribution, envelopes, glide |
| `drums.h/.cpp` | DRUMS: 10 parametric voices, DJ filter |
| `looper.h/.cpp` | Event loop: record, overdub, tempo, clear |
| `fastmath.h/.cpp` | Fixed-point helpers, sine LUT, PRNG |
| `ComputerCard.h` | Vendored MTM library — **do not edit** |
| `tools/` | Python models (see below) + `syntax.sh` |
| `info.yaml` | Workshop System card registry metadata |

## Verifying changes

**There is no host C++ compiler on this machine.** Two things fill the gap:

```sh
sh tools/syntax.sh          # type-check every .cpp with the ARM compiler, ~1s
python tools/ghostsim.py    # the ghost rule + learn round-trip
python tools/dspsim.py      # DJ filter stability, soft clip
python tools/loopsim.py     # event ordering, overdub, tempo
```

`tools/syntax.sh` does not link, so it cannot catch a missing symbol — but it
catches every syntax/type error in about a second, which is most of what goes
wrong. Run a real `cmake --build` before believing anything.

The Python models are **line-by-line ports** of the C++ they mirror. If you
change `levels.cpp`, `drums.cpp` or `looper.cpp`, change them too — or delete
them rather than let them drift into telling you a comfortable lie. Between
them they have already caught four real bugs that would each have been hard to
diagnose by ear:

- a one-pole slew stalling asymmetrically, so a settled reading depended on
  which combo it was approached from;
- a soft clipper with 0.33× gain, i.e. the whole kit a third too quiet;
- a looper sorting events by raw tick when quantisation can move them across
  the loop boundary;
- a looper "catch up on overdue events" condition that double-fired every hit
  landing on tick 0.

`tools/crosscheck.py` compiles `levels.cpp` for the host and diffs it against
the Python model. It **skips cleanly** when no host compiler is present, which
is the case here — it is there for when one is.

## Repo

`https://github.com/uglifruit/WorkshopButtons` (public). Commit as
Andy Jenkinson (uglifruit).
