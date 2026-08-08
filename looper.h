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
#include "drums.h"

namespace nib {

/// One recorded event. Exactly 4 bytes; the array is the card's largest single
/// allocation and it is still only 2KB.
struct LoopEvent
{
	uint16_t tick;    ///< 0..kLoopTicks-1, RAW (unquantised)
	uint8_t  what;    ///< 0..kNumVoices-1 = a drum VOICE, kFilterEvent = filter
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

/// Filter automation may never occupy more than this many slots.
///
/// This is a HARD budget, and it exists because automation and hits compete for
/// one array. A continuous knob sweep generates one event every
/// kFilterSampleTicks, which is ~96 per pass — so about five passes of idle
/// twiddling would fill all 512 slots, after which Insert() silently drops
/// everything and NEW DRUM HITS STOP BEING RECORDED. From the player's side
/// that feels exactly like the looper overwriting what they just played.
///
/// Automation also REPLACES rather than accumulates (see RecordFilter), so this
/// ceiling is generous: one pass of dense knob movement fits inside it with
/// room to spare, and hits always keep at least kMaxEvents - kMaxFilterEvents
/// slots to themselves.
constexpr int kMaxFilterEvents = 128;

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
	///
	/// Ignored while an external clock is running — see ClockPulse().
	void SetTempo(int32_t xKnob);

	/// An external clock edge arrived on Pulse In 1. Treated as one QUARTER
	/// NOTE, and it takes over from the knob for as long as it keeps arriving.
	///
	/// Rather than snapping the playhead (which would stutter the pattern on
	/// every edge), this measures the interval between edges and sets the tick
	/// rate from it. The loop then free-runs at the clock's tempo and stays
	/// phase-locked because each edge also re-aligns the playhead to the
	/// nearest beat — a correction of a tick or two, inaudible, rather than a
	/// jump.
	void ClockPulse();

	/// True while an external clock is driving the tempo.
	bool Clocked() const { return clockTimeout_ > 0; }

	/// Record a drum hit at the current position, by VOICE index.
	///
	/// Deliberately not by gesture: a pattern is a list of sounds, and how each
	/// was played belongs to the performance. Storing the gesture also meant a
	/// replayed hit had to re-derive its sound from a combination with no shift
	/// attached, and re-arranging the gesture map would silently change what an
	/// old loop played.
	///
	/// Ignored when the loop is full — dropping the 513th event is better than
	/// wrapping over the first.
	void RecordHit(int8_t voice, uint8_t velocity);

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

	/// True while the playhead is on a beat's first tick — the LEDs pulse on it.
	///
	/// A LEVEL, not an edge: it stays true for as long as that tick lasts, which
	/// at 40bpm is dozens of control ticks. Fine for an LED, useless for a
	/// trigger. Use BeatEdge() for anything that needs to fire once.
	bool OnBeat() const { return (playHead_ % kTicksPerBeat) == 0; }

	/// True exactly ONCE per beat, on the control tick that crosses into it.
	///
	/// This is what drives the click on Pulse Out 2. Consumed by reading, so
	/// call it once per Advance() and nowhere else.
	bool BeatEdge()
	{
		if (!beatEdge_) return false;
		beatEdge_ = false;
		return true;
	}

private:
	void Insert(const LoopEvent &ev);
	void Remove(int i);

	/// The tick at which an event actually sounds, after quantisation. The
	/// event array is sorted by THIS, not by the raw stored tick — see the
	/// comment in Insert().
	static uint16_t FireTick(const LoopEvent &ev);

	LoopEvent events_[kMaxEvents] = {};
	uint16_t  count_       = 0;
	uint16_t  filterCount_ = 0;   ///< how many of count_ are automation
	uint16_t  playHead_ = 0;
	uint16_t  cursor_   = 0;    ///< index of the next event at or after playHead_

	int32_t  phase_   = 0;      ///< Q16 fraction of a tick
	int32_t  tickInc_ = 0;      ///< Q16 ticks per control step
	int32_t  lastX_   = -9999;  ///< last tempo knob reading, for move detection

	// External clock. clockTimeout_ counts down at control rate; while it is
	// non-zero the knob is ignored. About 3 seconds, so a stopped clock hands
	// control back rather than freezing the loop.
	bool     beatEdge_ = false;   ///< set by Advance(), consumed by BeatEdge()
	int32_t  clockTimeout_ = 0;
	int32_t  clockInterval_ = 0;   ///< control ticks between the last two edges
	int32_t  sinceClock_    = 0;

	int32_t  lastFilter_     = -9999;
	int32_t  filterCountdown_ = 0;
};

} // namespace nib
