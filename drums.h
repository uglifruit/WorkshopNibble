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
	uint8_t  metal;       ///< 0 = normal, 1 = ring-modulated metallic (cymbals)
	uint16_t level;       ///< Q8 output gain, 256 = full. See below.
};

/// Why voices need their own level, rather than all starting at full scale.
///
/// Peak amplitude is a bad proxy for loudness once decay times differ by two
/// orders of magnitude. Every voice firing at 4095 means a 1.9-second crash
/// delivers roughly THIRTEEN TIMES the total energy of a kick and four hundred
/// times a closed hat — it was not slightly hot, it buried the kit.
///
/// The ear integrates over roughly a tenth of a second, so a long voice has to
/// start proportionally quieter to sit in the same mix. These are set by that
/// reasoning and then trimmed by ear.
constexpr uint16_t kLevelFull = 256;

/// How many distinct sounds the kit has.
///
/// Twelve, one per ordered gesture — because A-held-then-B and B-held-then-A
/// are different gestures even though they produce an identical voltage. See
/// LevelTracker::Shift() for how the ordering is recovered.
constexpr int kNumVoices = 12;

extern const DrumSpec kVoices[kNumVoices];
extern const int8_t   kGestureVoice[kNumSingles][kNumSingles];

/// Which VOICE a (shift, tap) gesture plays, or -1 if it is not a gesture.
///
/// The separation matters: the looper records the voice this returns, never the
/// gesture. How a hit was produced belongs to the performance, not the pattern.
static inline int8_t VoiceForGesture(int8_t shift, int8_t tap)
{
	if (shift < 0 || shift >= kNumSingles) return -1;
	if (tap   < 0 || tap   >= kNumSingles) return -1;
	return kGestureVoice[shift][tap];
}

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

/// Fractional headroom in the drum envelope accumulators.
///
/// Same trap as the KEYS envelopes, and it bit here too: `e -= (e >> shift) + 1`
/// reaches zero thanks to the `+1`, but that `+1` DOMINATES once `e >> shift`
/// rounds to zero, which for a 4095 peak happens around shift 12. Every longer
/// setting decayed in the same ~85ms, so shifts 12 and 13 were identical and a
/// "crash" was a blip.
///
/// Six bits of headroom puts the usable range at ~6ms to ~1.9s, which is what
/// a cymbal needs and leaves the Y knob's +/-3 meaningful at both ends.
constexpr int kDrumEnvFrac = 6;

class DrumVoice
{
public:
	void Trigger(const DrumSpec &spec, int32_t pitchScaleQ16, int32_t decayAdj);

	/// One audio-rate sample. Returns roughly -2047..2047; silent when idle.
	int32_t Step(uint32_t &rng);

	bool Active() const { return env_ > 0 || noiseEnv_ > 0; }

private:
	uint16_t phase_      = 0;
	uint16_t phase2_     = 0;   ///< second accumulator, metallic voices only
	uint16_t pitch_      = 0;
	uint16_t pitchFloor_ = 0;
	int32_t  env_        = 0;
	int32_t  noiseEnv_   = 0;
	uint16_t noiseMix_   = 0;
	uint8_t  decayShift_ = 10;
	uint8_t  noiseShift_ = 10;
	uint8_t  sweepRate_  = 0;
	uint8_t  sweepCount_ = 0;
	uint8_t  metal_      = 0;
	uint16_t level_      = kLevelFull;
};

/// How many voices can decay at once. Ten would be silly — you have four
/// fingers — but the ring must never steal a voice that is still audible, so a
/// little headroom over the realistic maximum is the cheap and correct answer.
constexpr int kMaxVoices = 6;

class DrumKit
{
public:
	/// Fire a VOICE by index. Both live playing and the looper come through
	/// here, so a replayed hit is bit-identical to the one that was played.
	///
	/// `yKnob` (0..4095) reshapes the whole kit: CCW lower and longer, CW higher
	/// and shorter. Applied at TRIGGER time only, so sweeping the knob does not
	/// warp voices that are already decaying — which would sound like a fault
	/// rather than a control.
	void TriggerVoice(int8_t voice, int32_t yKnob);

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
