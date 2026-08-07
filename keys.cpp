// keys.cpp — the keyboard mode's macro, envelopes and pitch output.

#include "keys.h"
#include "fastmath.h"
#include "pico.h"

namespace nib {

namespace {

// Envelope decay shifts. Larger shift = slower decay. At 48kHz, shift 6 is a
// click (~1.3ms) and shift 14 is a long swell (~340ms).
//
// Each destination gets its own band, because they are doing different jobs:
// the filter envelope wants to be able to go snappier than the others, and the
// length envelope wants to be able to go longer.
constexpr uint8_t kMinShift[kNumEnvs]   = { 8, 6, 7 };   // length, filter, loudness
constexpr uint8_t kShiftRange[kNumEnvs] = { 6, 5, 5 };

/// Full-scale envelope output. The audio outs run -2048..2047 and the envelopes
/// are unipolar, so this is the positive ceiling.
constexpr int32_t kEnvFullScale = 2047;

/// Millivolts per semitone, x256. 1V/oct = 1000mV per 12 semitones, so one
/// semitone is 83.333mV, kept in Q8.
///
/// Convert with `(note * kMvPerSemiQ8 + 128) >> 8` — the +128 ROUNDS instead of
/// truncating, and it matters. Truncating is biased one way at every octave
/// (every C landed a full millivolt flat, 1.2 cents, which is audible against a
/// tuned oscillator); rounding brings the worst case over the whole 0..127
/// range down to 0.33mV, or 0.4 cents, which is not.
constexpr int32_t kMvPerSemiQ8 = 21333;   // round(1000/12 * 256)

} // namespace

// ---------------------------------------------------------------------------
// The macro
// ---------------------------------------------------------------------------

void Keys::SetMacro(int32_t mainKnob, int32_t xKnob)
{
	const int32_t m = knob_to_q16(mainKnob);    // intensity
	const int32_t x = knob_to_q16(xKnob);       // distribution

	// Three anchor relationships, crossfaded so the X knob is continuous and
	// there is no audible step where they meet.
	//
	//   LONG      long decays, filter mostly closed, loudness always present
	//   BALANCED  all three follow the macro equally
	//   BRIGHT    short decays, filter open, loudness squared into an accent
	int32_t aL, aF, aA, bL, bF, bA, t;

	if (x < kQ16One / 2)
	{
		t = x * 2;
		// LONG
		aL = m;
		aF = mul_q16(m, 16384);                     // 25% of macro
		aA = 39322 + mul_q16(m, 26214);             // 60% floor + 40% macro
		// BALANCED
		bL = m; bF = m; bA = m;
	}
	else
	{
		t = (x - kQ16One / 2) * 2;
		// BALANCED
		aL = m; aF = m; aA = m;
		// BRIGHT
		bL = 13107 + mul_q16(m, 19661);             // 20% floor + 30% macro
		bF = m;
		bA = mul_q16(m, m);                         // squared: the top end pops
	}

	const int32_t amount[kNumEnvs] = {
		aL + mul_q16(bL - aL, t),
		aF + mul_q16(bF - aF, t),
		aA + mul_q16(bA - aA, t),
	};

	for (int i = 0; i < kNumEnvs; i++)
	{
		int32_t a = amount[i];
		if (a < 0)        a = 0;
		if (a > kQ16One)  a = kQ16One;

		// The same amount sets BOTH the peak and the decay length. One knob
		// moving both is what makes the macro feel like an instrument control
		// rather than three separate faders: a louder hit is also a longer hit,
		// which is how acoustic sources behave.
		env_[i].peak = (a * kEnvFullScale) >> 16;

		// sqrt bends the law so the bottom of the travel moves fastest, which
		// is where the audible difference between "click" and "short" lives.
		int32_t curve = fast_sqrt_q16(a);
		env_[i].shift = static_cast<uint8_t>(
			kMinShift[i] + ((curve * kShiftRange[i]) >> 16));
	}
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

void Keys::NoteOn(uint8_t midiNote)
{
	note_ = midiNote;

	// Pitch is carried in millivolts rather than as a MIDI note so that glide
	// has something continuous to move through. CVOutMillivolts() applies the
	// same EEPROM calibration as CVOutMIDINote(), so tuning is unaffected.
	targetMv_ = (static_cast<int32_t>(midiNote) * kMvPerSemiQ8 + 128) >> 8;

	// First note after boot should not glide up from zero — that is a swoop
	// nobody asked for.
	if (!glideInit_) { glideMv_ = targetMv_; glideInit_ = true; }

	Retrigger();
}

void Keys::Retrigger()
{
	for (int i = 0; i < kNumEnvs; i++) env_[i].Trigger();
}

int32_t __not_in_flash_func(Keys::PitchMillivolts)(bool glide)
{
	if (!glide)
	{
		glideMv_ = targetMv_;
		return targetMv_;
	}

	// "Fast glide": a short portamento, not a lazy slide. Shift 9 at 48kHz
	// covers a semitone in a few milliseconds and an octave in around 40 —
	// audible as a bend rather than as a wait.
	//
	// slew_exact, not slew: a stalling slew would leave the pitch permanently
	// a few millivolts flat of the note it was heading for, which on a 1V/oct
	// output is an audible detune rather than a rounding error.
	glideMv_ = slew_exact(glideMv_, targetMv_, 9);
	return glideMv_;
}

void __not_in_flash_func(Keys::StepEnvelopes)(int32_t out[kNumEnvs])
{
	for (int i = 0; i < kNumEnvs; i++) out[i] = env_[i].Step();
}

} // namespace nib
