// keys.h — the default boot mode: a ten-note keyboard.
//
// A combo becomes a scale degree becomes a 1V/oct pitch, plus three envelopes
// shaped by one macro knob.
//
//   CV Out 1     1V/oct pitch, calibrated. Stepped, or glided when Switch UP.
//   CV Out 2     LENGTH envelope   -- decay/gate-length for an external EG
//   Audio Out 1  FILTER envelope   -- snappier, for a filter cutoff
//   Audio Out 2  LOUDNESS envelope -- amplitude, with the accent behaviour
//   Pulse Out 1  gate on every note event
//
// The audio outs are DC-coupled so they work as CV, but they are NOT calibrated
// the way the CV outs are. They are fine driving envelope and filter inputs;
// they are not accurate enough for pitch, and the README says so.

#pragma once
#include <stdint.h>
#include "nibble.h"

namespace nib {

/// The three envelope destinations, in output order.
enum EnvDest : uint8_t { kEnvLength = 0, kEnvFilter = 1, kEnvLoudness = 2 };
constexpr int kNumEnvs = 3;

/// Lowest note the keyboard sits at, before transpose. C2 — low enough that
/// two and a half octaves of degrees stays in useful bass/mid territory.
constexpr int kBaseNote = 36;

/// Transpose range from CV In 2, in semitones either way.
constexpr int kTransposeSemis = 24;

/// A one-shot decay envelope. `env` is Q16-ish and simply decays by a shift per
/// sample — the WildPebble idiom, `e -= (e >> shift) + 1`, which is
/// exponential-shaped and guaranteed to actually reach zero.
struct Env
{
	int32_t value = 0;
	int32_t peak  = 0;
	uint8_t shift = 10;

	void Trigger() { value = peak; }

	/// One audio-rate step. Envelopes run at 48kHz so they are smooth and
	/// clickless with no separate slew stage.
	inline int32_t Step()
	{
		if (value > 0)
		{
			value -= (value >> shift) + 1;
			if (value < 0) value = 0;
		}
		return value;
	}
};

class Keys
{
public:
	/// Recompute the macro distribution. Control rate; `main` and `x` are raw
	/// knob readings, 0..4095.
	void SetMacro(int32_t mainKnob, int32_t xKnob);

	/// Fire all three envelopes and latch a new pitch.
	void NoteOn(uint8_t midiNote);

	/// Re-fire the envelopes without changing pitch (momentary tap, Pulse In).
	void Retrigger();

	/// One audio-rate step. Returns the three envelope levels, 0..2047.
	void StepEnvelopes(int32_t out[kNumEnvs]);

	/// Target pitch in millivolts, for the caller to send to CV Out 1.
	/// `glide` slews toward it; otherwise it steps.
	int32_t PitchMillivolts(bool glide);

	uint8_t Note() const { return note_; }

private:
	Env      env_[kNumEnvs];
	uint8_t  note_       = kBaseNote;
	int32_t  targetMv_   = 0;
	int32_t  glideMv_    = 0;
	bool     glideInit_  = false;
};

} // namespace nib
