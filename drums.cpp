// drums.cpp — the kit and the DJ filter.

#include "drums.h"
#include "fastmath.h"
#include "pico.h"

namespace nib {

// ---------------------------------------------------------------------------
// The kit
// ---------------------------------------------------------------------------
//
// Combo order is A B C D AB AC AD BC BD CD. Kick and snare are on A and B so
// the two sounds a pattern is mostly made of need one finger each; the toms sit
// on pairs sharing a finger, so hold-A-and-tap gives a tom run.

// The TWELVE ordered voices, indexed [shift][tap].
//
// Diagonal entries (shift == tap) are never reached — you cannot hold a button
// and tap the same one — and are left as zeroes.
//
// The layout is chosen for the HAND, not for tidiness. Holding C with a right
// thumb leaves A, B and D under the fingers, so that shift carries the three
// sounds a beat is actually made of: closed hat, snare, kick. Holding D gives
// the tom/syn-drum row for fills. A and B carry the colour: cowbell, hats, and
// the crash.
const DrumSpec kOrdered[kNumSingles][kNumSingles] = {
	// ---- HOLD A ----
	{
		{},                                          // A+A  (not a gesture)
		{ 1100, 1100,  7,  0,    0, 0, 0 },          // A+B  cowbell
		{ 1300, 1300,  5,  5,  256, 0, 1 },          // A+C  hi-hat, metallic alt
		{ 1300, 1300, 11, 11,  256, 0, 1 },          // A+D  open hi-hat
	},
	// ---- HOLD B ----
	{
		{ 1700, 1700, 15, 15,  256, 0, 1 },          // B+A  CRASH (long, metal)
		{},                                          // B+B
		{  180,    4, 12,  0,    0, 3, 0 },          // B+C  kick, deep alt
		{  330,   30,  8, 10,  200, 1, 0 },          // B+D  snare, snappy alt
	},
	// ---- HOLD C ----  the workhorse row, under a right hand
	{
		{  900,  900,  6,  6,  256, 0, 0 },          // C+A  closed hat
		{  400,   24,  9,  7,  140, 1, 0 },          // C+B  snare
		{},                                          // C+C  (not a gesture)
		{  220,    4, 11,  0,    0, 2, 0 },          // C+D  kick
	},
	// ---- HOLD D ----  toms and syn drums, for fills
	{
		{  600,   60,  9,  0,    0, 1, 0 },          // D+A  "pew" tom 1
		{  420,  140, 11,  8,   20, 1, 0 },          // D+B  syn tom 2
		{  520,  110, 10,  6,   40, 1, 0 },          // D+C  syn drum 3
		{},                                          // D+D  (not a gesture)
	},
};

// Both indices are plain button numbers 0..3, so this is a direct lookup — the
// diagonal entries are the unreachable "hold and tap the same button" slots and
// are never read.
const DrumSpec *OrderedVoice(int8_t shift, int8_t tap)
{
	if (shift < 0 || shift >= kNumSingles) return nullptr;
	if (tap   < 0 || tap   >= kNumSingles) return nullptr;
	if (shift == tap) return nullptr;
	return &kOrdered[shift][tap];
}

// The old combination-indexed kit, still used by the LOOPER: recorded events
// store a combo, and a bare single fired from a loop should make some sound
// rather than none.
const DrumSpec kKit[kNumLevels] = {
	// pitch0 floor  decay noise  mix  sweep metal
	{  300,   90,     11,    8,    20,   1,   0 },   // A  low tom
	{  420,  140,     11,    8,    20,   1,   0 },   // B  mid tom
	{  560,  200,     10,    8,    20,   1,   0 },   // C  high tom
	{ 1100, 1100,      7,    0,     0,   0,   0 },   // D  cowbell
	{  220,    4,     12,    0,     0,   2,   0 },   // AB kick
	{  900,  900,      6,    6,   256,   0,   0 },   // AC closed hat
	{  500,  500,      8,    8,   256,   0,   0 },   // AD clap
	{  900,  900,     10,   10,   256,   0,   0 },   // BC open hat
	{  700,  700,      5,    5,    40,   0,   0 },   // BD rim
	{  400,   24,      9,    7,   140,   1,   0 },   // CD snare
};

namespace {

/// Cubic soft clip: y = x - x^3/(3T^2) below the knee T, hard at +/-kLimit
/// above it. Several voices summing at full scale can reach many times the DAC
/// range, so this is load-bearing, not decoration.
///
/// UNITY GAIN below the knee, which is the property to protect. An earlier
/// version normalised the input by 3*kLimit but scaled the output back by only
/// kLimit, giving every signal a gain of 0.33 — the whole kit played a third
/// too quiet, which is exactly the kind of wrongness that sounds plausible.
/// tools/dspsim.py asserts small-signal unity now.
///
/// The knee is 1.5*kLimit because the cubic reaches its maximum of (2/3)T
/// there, and (2/3)(1.5 kLimit) == kLimit exactly.
inline int32_t SoftClip(int32_t x)
{
	constexpr int32_t kLimit = 2047;
	constexpr int32_t kKnee  = (3 * kLimit) / 2;

	if (x >  kKnee) return  kLimit;
	if (x < -kKnee) return -kLimit;

	// Split the cube so the intermediate stays inside 32 bits: (x*x/T)*x/(3T)
	// peaks around x*2/3, far below the limit.
	return x - (((x * x) / kKnee) * x) / (3 * kKnee);
}

} // namespace

// ---------------------------------------------------------------------------
// One voice
// ---------------------------------------------------------------------------

void DrumVoice::Trigger(const DrumSpec &spec, int32_t pitchScaleQ16, int32_t decayAdj)
{
	int32_t p0 = mul_q16(spec.pitch0,     pitchScaleQ16);
	int32_t pf = mul_q16(spec.pitchFloor, pitchScaleQ16);
	if (p0 < 4) p0 = 4;
	if (pf < 4) pf = 4;
	if (p0 > 4095) p0 = 4095;
	if (pf > 4095) pf = 4095;

	phase_      = 0;
	phase2_     = 0;
	pitch_      = static_cast<uint16_t>(p0);
	pitchFloor_ = static_cast<uint16_t>(pf);
	noiseMix_   = spec.noiseMix;
	sweepRate_  = spec.sweepRate;
	sweepCount_ = 0;
	metal_      = spec.metal;

	// decayAdj shifts the whole kit shorter or longer. Clamped so the extremes
	// stay musical rather than becoming a click or a drone.
	int32_t ds = spec.decayShift + decayAdj;
	int32_t ns = spec.noiseShift + decayAdj;
	if (ds < 4)  ds = 4;
	if (ds > 15) ds = 15;
	if (ns < 4)  ns = 4;
	if (ns > 15) ns = 15;
	decayShift_ = static_cast<uint8_t>(ds);
	noiseShift_ = static_cast<uint8_t>(ns);

	env_      = 4095 << kDrumEnvFrac;
	noiseEnv_ = (spec.noiseMix > 0) ? (4095 << kDrumEnvFrac) : 0;
}

int32_t __not_in_flash_func(DrumVoice::Step)(uint32_t &rng)
{
	if (env_ <= 0 && noiseEnv_ <= 0) return 0;

	// --- body: a triangle folded out of a 12-bit phase accumulator ---
	phase_ = static_cast<uint16_t>((phase_ + pitch_) & 4095);

	int32_t osc;
	if (phase_ & 2048) osc = 2048 - (phase_ & 2047);
	else               osc = phase_ & 2047;
	osc = (osc - 1024) << 1;                      // centre and scale to +/-2047

	// Pitch sweep. A falling pitch during the decay is what makes a kick read
	// as a kick rather than as a short bass note.
	if (sweepRate_ && pitch_ > pitchFloor_)
	{
		if (++sweepCount_ >= sweepRate_)
		{
			sweepCount_ = 0;
			pitch_--;
		}
	}

	// --- noise ---
	int32_t noise = 0;
	if (noiseEnv_ > (8 << kDrumEnvFrac))
		noise = (static_cast<int32_t>((xorshift32(rng) >> 24) & 255) - 128) << 4;

	// METALLIC voices ring-modulate the noise against a second, inharmonically
	// related square. Plain filtered noise reads as "shh"; cymbals need the
	// clangorous, slightly pitched quality that comes from beating partials,
	// and a ring mod is the cheapest honest way to get it — two phase
	// accumulators and a sign flip, no extra filters.
	//
	// The 1.47x ratio is deliberately not a simple fraction: an integer ratio
	// would lock the two into a harmonic relationship and sound like a tone.
	if (metal_)
	{
		phase2_ = static_cast<uint16_t>((phase2_ + ((pitch_ * 3) / 2 + 37)) & 4095);
		if (phase2_ & 2048) noise = -noise;
		// Square the body too, so the metal voices are all edge and no thud.
		osc = (phase_ & 2048) ? 2047 : -2047;
	}

	// --- decays ---
	if (env_ > 0)
	{
		env_ -= (env_ >> decayShift_) + 1;
		if (env_ < 0) env_ = 0;
	}
	if (noiseEnv_ > 0)
	{
		noiseEnv_ -= (noiseEnv_ >> noiseShift_) + 1;
		if (noiseEnv_ < 0) noiseEnv_ = 0;
	}

	// --- mix body and noise by the voice's character ---
	int32_t body = (osc   * (env_      >> kDrumEnvFrac)) >> 12;
	int32_t nz   = (noise * (noiseEnv_ >> kDrumEnvFrac)) >> 12;
	return ((body * (256 - noiseMix_)) + (nz * noiseMix_)) >> 8;
}

// ---------------------------------------------------------------------------
// The kit
// ---------------------------------------------------------------------------

void DrumKit::TriggerOrdered(int8_t shift, int8_t tap, int32_t yKnob)
{
	const DrumSpec *spec = OrderedVoice(shift, tap);
	if (!spec) return;

	int32_t y = knob_to_q16(yKnob);
	int32_t pitchScale = 32768 + y;
	int32_t decayAdj   = 3 - ((yKnob * 6) >> 12);

	voice_[next_].Trigger(*spec, pitchScale, decayAdj);
	next_ = static_cast<uint8_t>((next_ + 1) % kMaxVoices);
}

void DrumKit::Trigger(int8_t combo, int32_t yKnob)
{
	if (combo < 0 || combo >= kNumLevels) return;

	// Y sweeps the whole kit's character in one gesture: pitch from half to
	// double, and decay from three shifts longer to three shorter. Centre
	// detented so the stock kit is easy to find.
	int32_t y = knob_to_q16(yKnob);                  // 0..65536
	int32_t pitchScale = 32768 + y;                  // Q16 0.5 .. 1.5
	int32_t decayAdj   = 3 - ((yKnob * 6) >> 12);    // +3 .. -3

	voice_[next_].Trigger(kKit[combo], pitchScale, decayAdj);
	next_ = static_cast<uint8_t>((next_ + 1) % kMaxVoices);
}

int32_t __not_in_flash_func(DrumKit::Step)()
{
	int32_t sum = 0;
	for (int i = 0; i < kMaxVoices; i++) sum += voice_[i].Step(rng_);
	return SoftClip(sum);
}

// ---------------------------------------------------------------------------
// DJ filter
// ---------------------------------------------------------------------------

namespace {

/// Knob position -> filter coefficient, with a perceptually exponential taper.
/// The shape (linear + quadratic + cubic, weighted) is from 45_bends; a linear
/// sweep spends most of its travel in a range the ear reads as "already open".
inline int32_t CutoffCurve(int32_t ratioQ15)
{
	int32_t quad  = (ratioQ15 * ratioQ15) >> 15;
	int32_t cubic = (quad * ratioQ15) >> 15;
	int32_t mixed = (ratioQ15 * 4000 + quad * 10000 + cubic * 18768) >> 15;
	int32_t g = 300 + mixed;
	if (g < 150)   g = 150;
	if (g > 22000) g = 22000;
	return g;
}

/// Fixed mild resonance, Q15. No knob is free for it, and a fixed slight
/// emphasis is what a DJ filter sounds like anyway.
constexpr int32_t kResonance = 12000;

/// Bypass deadband around centre. Generous on purpose: you have to be able to
/// find "no filtering" by feel, mid-performance, without looking at the panel.
constexpr int32_t kBypassLo = 1800;
constexpr int32_t kBypassHi = 2300;

} // namespace

void DjFilter::SetKnob(int32_t knob)
{
	if (knob < kBypassLo)
	{
		// Low-pass: fully CCW is nearly closed, opening toward centre.
		mode_ = -1;
		g_ = CutoffCurve((knob << 15) / kBypassLo);
	}
	else if (knob > kBypassHi)
	{
		// High-pass: opens as the knob climbs above centre.
		mode_ = 1;
		g_ = CutoffCurve(((knob - kBypassHi) << 15) / (4095 - kBypassHi));
	}
	else
	{
		mode_ = 0;
	}
}

int32_t __not_in_flash_func(DjFilter::Step)(int32_t in)
{
	if (mode_ == 0)
	{
		// Bypassed. Bleed the state toward zero rather than freezing it, so
		// coming back out of bypass does not thump with a stale charge.
		v1_ -= v1_ >> 6;
		v2_ -= v2_ >> 6;
		return in;
	}

	// Chamberlin SVF, in the order 45_bends documents as its correctness fix:
	// compute the high-pass from the PREVIOUS states, integrate, then derive
	// the low-pass from the freshly updated band-pass.
	int32_t hp = in - ((kResonance * v1_) >> 15) - v2_;
	v1_ += (g_ * hp) >> 15;
	int32_t lp = v2_ + ((g_ * v1_) >> 15);
	v2_ = lp;

	// Clamp the states. At high g with resonance an SVF will happily blow up,
	// and an integer one wraps instead of merely getting loud — which is a
	// full-scale square wave, not a filter sound.
	constexpr int32_t kStateMax = 1 << 20;
	if (v1_ >  kStateMax) v1_ =  kStateMax;
	if (v1_ < -kStateMax) v1_ = -kStateMax;
	if (v2_ >  kStateMax) v2_ =  kStateMax;
	if (v2_ < -kStateMax) v2_ = -kStateMax;

	return (mode_ < 0) ? lp : hp;
}

} // namespace nib
