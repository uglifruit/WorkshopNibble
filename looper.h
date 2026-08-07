// looper.h — the four-bar event loop behind DRUMS mode.
//
// This records EVENTS, not audio. That decision buys three things that matter:
//
//   1. OVERDUB IS LOSSLESS. Nothing is re-recorded, so a twentieth pass sounds
//      exactly like the first. An audio looper degrades a little every pass.
//   2. TEMPO IS A PLAYBACK PARAMETER. A position is stored in loop TICKS, not
//      in samples, so turning the tempo knob re-times the pattern instead of
//      pitching it.
//   3. IT FITS. Four bytes per event, 512 events, 2KB total. One second of
//      48kHz audio would be 96KB.
//
// Quantisation is applied at PLAYBACK, never at capture: the raw tick is what
// gets stored. That keeps it non-destructive, so a later version can add swing
// or humanise without having thrown the original timing away.

#pragma once
#include <stdint.h>
#include "nibble.h"

namespace nib {

/// One recorded event. Exactly 4 bytes; the array is the card's largest single
/// allocation and it is still only 2KB.
struct LoopEvent
{
	uint16_t tick;    ///< 0..kLoopTicks-1, RAW (unquantised)
	uint8_t  what;    ///< 0..9 = drum combo, kFilterEvent = filter automation
	uint8_t  value;   ///< drum: velocity. filter: knob >> 4.
};

/// Marks an automation event rather than a drum hit.
constexpr uint8_t kFilterEvent = 0x80;

/// Four bars of 4/4 at 48 ticks per beat. 48 divides by 2, 3, 4, 6, 8, 12 and
/// 16, so every useful subdivision (and triplets) lands on an exact tick.
constexpr int kTicksPerBeat = 48;
constexpr int kBeatsPerLoop = 16;
constexpr int kLoopTicks    = kTicksPerBeat * kBeatsPerLoop;   // 768

/// Playback quantisation: 1/16 notes. A lo-fi drum looper played with fingers
/// on a resistor network needs it.
constexpr int kQuantTicks = kTicksPerBeat / 4;                 // 12

constexpr int kMaxEvents = 512;

/// Tempo range. 40-240 BPM across the X knob.
constexpr int32_t kBpmMin = 40;
constexpr int32_t kBpmMax = 240;

/// How often the filter knob is sampled while recording, in ticks. Caps
/// automation density at ~6 events/beat worst case and usually near zero,
/// since an event is only emitted when the knob has actually moved.
constexpr int kFilterSampleTicks = 8;

class Looper
{
public:
	/// Advance time. Returns true on each tick boundary crossed; the caller
	/// fires whatever events that tick carries. Control rate.
	bool Advance();

	/// Set tempo from the X knob (0..4095). Recomputes the tick increment ONLY
	/// when the knob has moved: the division is a libgcc call of ~100 cycles on
	/// an M0+, which is fine a few times a second and unacceptable per tick.
	void SetTempo(int32_t xKnob);

	/// Record a drum hit at the current position. Ignored when the loop is
	/// full — dropping the 513th event is better than wrapping over the first.
	void RecordHit(int8_t combo, uint8_t velocity);

	/// Record a filter-knob position, if it has moved enough to be worth it.
	void RecordFilter(int32_t knob);

	/// Fire every event scheduled for the current tick.
	/// `outCombo` receives drum hits; `outFilter` receives automation.
	/// Returns the number of drum hits written (0..kMaxFirePerTick).
	static constexpr int kMaxFirePerTick = 8;
	int Fire(int8_t *outCombo, uint8_t *outVel, int32_t *outFilter, bool *haveFilter);

	void Clear();

	uint16_t Position() const   { return playHead_; }
	uint16_t EventCount() const { return count_; }
	bool     Full() const       { return count_ >= kMaxEvents; }

	/// True on the first tick of each beat — the LEDs pulse on this.
	bool OnBeat() const { return (playHead_ % kTicksPerBeat) == 0; }

private:
	void Insert(const LoopEvent &ev);

	/// The tick at which an event actually sounds, after quantisation. The
	/// event array is sorted by THIS, not by the raw stored tick — see the
	/// comment in Insert().
	static uint16_t FireTick(const LoopEvent &ev);

	LoopEvent events_[kMaxEvents] = {};
	uint16_t  count_    = 0;
	uint16_t  playHead_ = 0;
	uint16_t  cursor_   = 0;    ///< index of the next event at or after playHead_

	int32_t  phase_   = 0;      ///< Q16 fraction of a tick
	int32_t  tickInc_ = 0;      ///< Q16 ticks per control step
	int32_t  lastX_   = -9999;  ///< last tempo knob reading, for move detection

	int32_t  lastFilter_     = -9999;
	int32_t  filterCountdown_ = 0;
};

} // namespace nib
