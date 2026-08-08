// keys.h — the default boot mode: a ten-note keyboard.
//
// A combo becomes a scale degree becomes a 1V/oct pitch, plus a harmony voice
// and two envelopes shaped by one macro knob.
//
//   CV Out 1     1V/oct pitch, calibrated. Stepped, or glided when Switch UP.
//   CV Out 2     1V/oct HARMONY, a third above in the current scale
//   Audio Out 1  FILTER envelope   -- snappier, for a filter cutoff
//   Audio Out 2  LOUDNESS envelope -- amplitude / how long the note lasts
//   Pulse Out 1  gate on every note event
//
// The audio outs are DC-coupled so they work as CV, but they are NOT calibrated
// the way the CV outs are. They are fine driving envelope and filter inputs;
// they are not accurate enough for pitch, and the README says so.

#pragma once
#include <stdint.h>
#include "nibble.h"

namespace nib {

/// The envelope destinations.
///
/// There were three. LENGTH is gone: on the panel it turned out to be
/// indistinguishable from LOUDNESS — both are "how long does this note last" —
/// so it was a control that cost an output and told you nothing new. CV Out 2
/// now carries a HARMONY pitch instead, which is something the card could not
/// do at all before.
enum EnvDest : uint8_t { kEnvFilter = 0, kEnvLoudness = 1 };
constexpr int kNumEnvs = 2;

// How far above the played note the harmony sits is per-scale, and lives with
// the scale tables: see HarmonyDegreesFor() in scales.h.

/// Lowest note the keyboard sits at, before transpose. C2 — low enough that
/// two and a half octaves of degrees stays in useful bass/mid territory.
constexpr int kBaseNote = 36;

/// Transpose range from CV In 2, in semitones either way.
constexpr int kTransposeSemis = 24;

/// Extra fractional bits the envelope accumulator carries.
///
/// This is not a refinement — without it the envelope CANNOT be made long.
/// `e -= (e >> shift) + 1` reaches zero because of the `+1`, but that same `+1`
/// dominates as soon as `e >> shift` rounds to zero, which for a peak of 2047
/// happens around shift 11. Every shift above that decayed in the SAME 43ms,
/// so the whole top half of the macro knob did nothing at all and the longest
/// available envelope was a blip.
///
/// Running the accumulator 8 bits higher keeps the shift meaningful for far
/// longer: the range becomes ~13ms to ~11s, and the knob works across its
/// whole travel.
constexpr int kEnvFrac = 8;

/// A one-shot decay envelope. Exponential-shaped via the WildPebble idiom,
/// `e -= (e >> shift) + 1`, which is guaranteed to actually reach zero rather
/// than leaving a DC tail.
struct Env
{
	int32_t value = 0;      ///< scaled up by kEnvFrac
	int32_t peak  = 0;      ///< likewise
	uint8_t shift = 12;

	void Trigger() { value = peak; }

	/// One audio-rate step. Envelopes run at 48kHz so they are smooth and
	/// clickless with no separate slew stage. Returns the OUTPUT-scale value.
	inline int32_t Step()
	{
		if (value > 0)
		{
			value -= (value >> shift) + 1;
			if (value < 0) value = 0;
		}
		return value >> kEnvFrac;
	}
};

class Keys
{
public:
	/// Recompute the macro distribution. Control rate; `main` and `x` are raw
	/// knob readings, 0..4095.
	void SetMacro(int32_t mainKnob, int32_t xKnob);

	/// Fire the envelopes and latch a new pitch pair.
	/// `harmonyNote` is the in-scale third computed by the caller, which owns
	/// the root and scale.
	void NoteOn(uint8_t midiNote, uint8_t harmonyNote);

	/// Re-fire the envelopes without changing pitch (momentary tap, Pulse In).
	void Retrigger();

	/// One audio-rate step. Returns the envelope levels, 0..2047.
	void StepEnvelopes(int32_t out[kNumEnvs]);

	/// Target pitch in millivolts, for CV Out 1.
	/// `glide` slews toward it; otherwise it steps.
	int32_t PitchMillivolts(bool glide);

	/// Harmony pitch in millivolts, for CV Out 2. Follows the same glide
	/// setting, so the two voices move together rather than one sliding under
	/// the other.
	int32_t HarmonyMillivolts(bool glide);

	uint8_t Note() const { return note_; }

private:
	Env      env_[kNumEnvs];
	uint8_t  note_       = kBaseNote;
	int32_t  targetMv_   = 0;
	int32_t  glideMv_    = 0;
	int32_t  harmTarget_ = 0;
	int32_t  harmGlide_  = 0;
	bool     glideInit_  = false;
};

} // namespace nib
