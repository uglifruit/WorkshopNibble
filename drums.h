// drums.h — ten parametric percussion voices and the DJ filter.
//
// Everything is synthesised, not sampled. That is a deliberate choice and it
// buys three things: no flash cost, no sample-upload machinery (no USB, no web
// UI, no reserved flash region), and a Y knob that can reshape the whole kit
// simply by scaling parameters that are already live variables. Baked PCM would
// have turned "Y adjusts the sounds" into a resampling problem.
//
// The voice structure is generalised from 74_Wild_Pebble/WildPebble.cpp:787-901
// (Workshop System release 74): a phase accumulator masked to 12 bits, a
// triangle folded out of it, the pitch swept downward while the envelope runs,
// and exponential decay via `e -= (e >> shift) + 1`. Kick and snare there are
// the same code with different shifts; this makes that explicit and adds a
// noise-mix parameter so hats and claps come from the same struct.

#pragma once
#include <stdint.h>
#include "nibble.h"

namespace nib {

/// Per-voice character. One struct covers the whole kit.
struct DrumSpec
{
	uint16_t pitch0;      ///< starting phase increment
	uint16_t pitchFloor;  ///< sweep stops here (== pitch0 means no sweep)
	uint8_t  decayShift;  ///< body decay; larger = longer
	uint8_t  noiseShift;  ///< noise decay
	uint16_t noiseMix;    ///< Q8: 0 = pure tone, 256 = pure noise
	uint8_t  sweepRate;   ///< samples per pitch decrement; 0 = no sweep
};

/// The kit, indexed by combo. Kick and snare sit on the bare singles A and B:
/// the sounds you hit most often should need the fewest fingers.
extern const DrumSpec kKit[kNumLevels];

/// Semitone offsets for the four SINGLE buttons, sent out CV Out 1 in DRUMS.
///
/// The singles make no sound — they are shifts — so their CV output was going
/// spare. Giving them four pitches costs nothing and means that once a pattern
/// is looping you can play a simple bassline over it with the same four
/// buttons, without a second card or a mode switch.
///
///   A =  0   root
///   B = -2   a tone below, so the line can fall as well as rise
///   C = +7   the fifth
///   D = +12  the octave
///
/// A bonus, explicitly not the feature: no combos, no scale, no quantiser. Four
/// notes that happen to sit well together.
constexpr int8_t kBassSemis[kNumSingles] = { 0, -2, 7, 12 };

/// The bass line's root, in semitones above 0V.
///
/// One octave up rather than zero, because B is a TONE BELOW the root and the
/// CV output cannot usefully go negative — at a 0V root that note would be
/// -167mV, which most oscillators simply ignore. At 1V all four land between
/// 0.83V and 2V, which is bass register on a normally-tuned oscillator.
constexpr int kBassRoot = 12;

class DrumVoice
{
public:
	void Trigger(const DrumSpec &spec, int32_t pitchScaleQ16, int32_t decayAdj);

	/// One audio-rate sample. Returns roughly -2047..2047; silent when idle.
	int32_t Step(uint32_t &rng);

	bool Active() const { return env_ > 0 || noiseEnv_ > 0; }

private:
	uint16_t phase_      = 0;
	uint16_t pitch_      = 0;
	uint16_t pitchFloor_ = 0;
	int32_t  env_        = 0;
	int32_t  noiseEnv_   = 0;
	uint16_t noiseMix_   = 0;
	uint8_t  decayShift_ = 10;
	uint8_t  noiseShift_ = 10;
	uint8_t  sweepRate_  = 0;
	uint8_t  sweepCount_ = 0;
};

/// How many voices can decay at once. Ten would be silly — you have four
/// fingers — but the ring must never steal a voice that is still audible, so a
/// little headroom over the realistic maximum is the cheap and correct answer.
constexpr int kMaxVoices = 6;

class DrumKit
{
public:
	/// Fire the sound for a combo. `yKnob` (0..4095) reshapes the whole kit:
	/// CCW lower and longer, CW higher and shorter. Applied at TRIGGER time
	/// only, so sweeping the knob does not warp voices that are already
	/// decaying — which would sound like a fault rather than a control.
	void Trigger(int8_t combo, int32_t yKnob);

	/// Sum of every active voice, soft-clipped. Ten voices at full scale would
	/// reach +/-20470, so clipping is mandatory rather than a nicety.
	int32_t Step();

private:
	DrumVoice voice_[kMaxVoices];
	uint8_t   next_ = 0;
	uint32_t  rng_  = 0x1234567u;
};

// ---------------------------------------------------------------------------
// DJ filter
// ---------------------------------------------------------------------------

/// A mono Chamberlin state-variable filter with a DJ-isolator law on one knob:
/// low-pass below centre, high-pass above, and a generous bypass deadband in
/// the middle so "off" can be found by feel without looking.
///
/// Deliberately NOT 45_bends' FilterBlock, which is ~400 lines of stereo with a
/// compressor, a grit stage, two DC blockers and 1V/oct tracking bolted on.
/// What IS taken from that file is its update ORDER, which its own comments
/// flag as a correctness fix over an earlier version:
///
///     hp = in - r*v1 - v2;  v1 += g*hp;  lp = v2 + g*v1;  v2 = lp;
class DjFilter
{
public:
	/// `knob` is 0..4095. Control rate — the coefficients only need to move as
	/// fast as the knob does.
	void SetKnob(int32_t knob);

	/// One audio-rate sample.
	int32_t Step(int32_t in);

private:
	int32_t g_    = 0;        ///< Q15 frequency coefficient
	int32_t v1_   = 0;        ///< band-pass state
	int32_t v2_   = 0;        ///< low-pass state
	int8_t  mode_ = 0;        ///< -1 low-pass, 0 bypass, +1 high-pass
};

} // namespace nib
