// looper.cpp — event recording and playback.

#include "looper.h"
#include "fastmath.h"
#include "pico.h"

namespace nib {

namespace {

} // namespace

/// When an event actually sounds. Drum hits snap to the 1/16 grid; filter
/// automation deliberately does NOT, because a quantised sweep is a staircase.
///
/// The RAW tick is what gets stored — quantising here rather than at capture
/// keeps it non-destructive, so a later swing or humanise control still has the
/// original timing to work from. The divide is by a compile-time constant, so
/// the compiler strength-reduces it to a multiply and shift.
///
/// Note the wrap: a hit in the last half-step of the loop quantises UP to
/// kLoopTicks, which is tick 0 of the next pass, not a tick off the end.
uint16_t Looper::FireTick(const LoopEvent &ev)
{
	if (ev.what == kFilterEvent) return ev.tick;
	uint16_t q = static_cast<uint16_t>(((ev.tick + kQuantTicks / 2) / kQuantTicks)
	                                   * kQuantTicks);
	return static_cast<uint16_t>(q % kLoopTicks);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

void Looper::SetTempo(int32_t xKnob)
{
	// An external clock owns the tempo while it is running. Track the knob
	// position anyway, so that when the clock stops the knob does not have to
	// be wiggled before it takes effect again.
	if (Clocked()) { lastX_ = xKnob; return; }

	// Only recompute when the knob has actually moved. See the header.
	if (lastX_ >= 0 && (xKnob - lastX_ < kKnobMoveThresh)
	                && (lastX_ - xKnob < kKnobMoveThresh))
		return;
	lastX_ = xKnob;

	int32_t bpm = kBpmMin + ((xKnob * (kBpmMax - kBpmMin)) >> 12);

	// ticks/sec = bpm/60 * kTicksPerBeat; per control step, in Q16.
	tickInc_ = static_cast<int32_t>(
		(static_cast<int64_t>(bpm) * kTicksPerBeat * kQ16One)
		/ (60 * kCtrlRate));
}

void Looper::ClockPulse()
{
	// The first edge only starts the stopwatch; an interval needs two.
	if (sinceClock_ > 0 && sinceClock_ < kCtrlRate * 4)
	{
		clockInterval_ = sinceClock_;

		// One edge is a quarter note, so kTicksPerBeat ticks must elapse in
		// clockInterval_ control steps.
		tickInc_ = static_cast<int32_t>(
			(static_cast<int64_t>(kTicksPerBeat) * kQ16One) / clockInterval_);

		// Re-align to the nearest beat rather than snapping to it. Jumping the
		// playhead on every edge would stutter the pattern; nudging it keeps
		// the loop phase-locked while staying inaudible.
		int32_t offset = playHead_ % kTicksPerBeat;
		if (offset != 0)
		{
			if (offset > kTicksPerBeat / 2) offset -= kTicksPerBeat;
			// Pull back at most one tick per edge — enough to hold sync against
			// drift, small enough never to be heard as a skip.
			if (offset > 0)      playHead_--;
			else if (offset < 0) playHead_++;
			playHead_ = static_cast<uint16_t>((playHead_ + kLoopTicks) % kLoopTicks);
		}
	}

	sinceClock_   = 0;
	clockTimeout_ = kCtrlRate * 3;   // hand back to the knob ~3s after it stops
}

bool Looper::Advance()
{
	// Clock housekeeping FIRST and unconditionally: these are wall-clock
	// timers, not musical ones, and putting them after the early-outs below
	// would freeze them exactly when the tempo is zero or between ticks — so a
	// stopped external clock would never time out and hand control back.
	if (sinceClock_ < kCtrlRate * 8) sinceClock_++;
	if (clockTimeout_ > 0) clockTimeout_--;

	if (tickInc_ <= 0) return false;

	phase_ += tickInc_;
	if (phase_ < kQ16One) return false;

	// A single step never spans more than one tick at any supported tempo
	// (240bpm is 192 ticks/sec against a 3000Hz control rate), but loop rather
	// than subtract-once so a future faster tempo cannot silently drift.
	while (phase_ >= kQ16One)
	{
		phase_ -= kQ16One;
		playHead_ = static_cast<uint16_t>((playHead_ + 1) % kLoopTicks);

		// Latch the beat crossing here, where it happens exactly once, rather
		// than letting the caller poll OnBeat() — that is a level and stays true
		// for the whole tick, which at 40bpm is dozens of control steps and
		// would give a click that is on more than it is off.
		if ((playHead_ % kTicksPerBeat) == 0) beatEdge_ = true;

		// Rewinding the cursor at the loop boundary is what makes playback a
		// walk rather than a search.
		if (playHead_ == 0) cursor_ = 0;
	}

	if (filterCountdown_ > 0) filterCountdown_--;
	return true;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void Looper::Insert(const LoopEvent &ev)
{
	if (count_ >= kMaxEvents) return;

	// Keep the array sorted by FIRE time — not by the raw stored tick.
	//
	// This distinction is load-bearing. Quantisation can move an event across
	// the loop boundary (a hit at tick 5 fires at tick 0; a hit at tick 763
	// fires at tick 0 of the NEXT pass), so an array sorted by raw tick is not
	// sorted by the order things actually sound, and the cursor walk in Fire()
	// then double-fires or strands events. Sorting by fire time restores the
	// invariant the walk depends on. tools/loopsim.py caught this as a hit
	// sounding twice per pass.
	//
	// An insertion-sort step: O(n) worst case at 512 entries, but it happens at
	// most a few times a second at control rate and is bounded, so it is
	// affordable even inside the interrupt.
	const uint16_t evWhen = FireTick(ev);
	int i = count_;
	while (i > 0 && FireTick(events_[i - 1]) > evWhen)
	{
		events_[i] = events_[i - 1];
		i--;
	}
	events_[i] = ev;
	count_++;
	if (ev.what == kFilterEvent) filterCount_++;

	// The new event may sit before the cursor, which would make the cursor
	// point at the wrong place for the rest of this pass. Nudging it keeps the
	// invariant "cursor_ is the first event at or after playHead_".
	if (static_cast<uint16_t>(i) < cursor_) cursor_++;
}

void Looper::RecordHit(int8_t combo, uint8_t velocity)
{
	if (combo < 0 || combo >= kNumLevels) return;

	// If the array is full but automation is hogging it, evict the oldest
	// automation event to make room. A drum hit the player just performed
	// always matters more than a knob position from three passes ago — and
	// silently dropping it is what made the looper feel like it was
	// overwriting things.
	if (count_ >= kMaxEvents && filterCount_ > 0)
	{
		for (int i = 0; i < count_; i++)
			if (events_[i].what == kFilterEvent) { Remove(i); break; }
	}

	LoopEvent ev;
	ev.tick  = playHead_;
	ev.what  = static_cast<uint8_t>(combo);
	ev.value = velocity;
	Insert(ev);
}

void Looper::RecordFilter(int32_t knob)
{
	if (filterCountdown_ > 0) return;
	filterCountdown_ = kFilterSampleTicks;

	if (lastFilter_ >= 0 && (knob - lastFilter_ < kKnobMoveThresh)
	                     && (lastFilter_ - knob < kKnobMoveThresh))
		return;
	lastFilter_ = knob;

	// Automation REPLACES itself rather than accumulating. A knob pass on top of
	// an earlier one should read as "I redid that sweep", not as two sweeps
	// fighting on the same tick — and without this, every pass adds another
	// ~96 events until the buffer is full and drum hits start being dropped.
	//
	// Drop any existing automation already sitting on this tick first.
	for (int i = 0; i < count_; )
	{
		if (events_[i].what == kFilterEvent && FireTick(events_[i]) == playHead_)
			Remove(i);
		else
			i++;
	}

	if (filterCount_ >= kMaxFilterEvents) return;

	LoopEvent ev;
	ev.tick  = playHead_;
	ev.what  = kFilterEvent;
	ev.value = static_cast<uint8_t>(knob >> 4);   // 0..4095 -> 0..255
	Insert(ev);
}

/// Remove the event at `i`, keeping the array sorted and the cursor honest.
void Looper::Remove(int i)
{
	if (i < 0 || i >= count_) return;
	if (events_[i].what == kFilterEvent && filterCount_ > 0) filterCount_--;

	for (int j = i; j + 1 < count_; j++) events_[j] = events_[j + 1];
	count_--;

	// Keep "cursor_ is the next event to fire" true. Removing something the
	// cursor has already passed must pull the cursor back with it, or the rest
	// of this pass fires the wrong events.
	if (static_cast<uint16_t>(i) < cursor_ && cursor_ > 0) cursor_--;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

int Looper::Fire(int8_t *outCombo, uint8_t *outVel,
                 int32_t *outFilter, bool *haveFilter)
{
	int n = 0;
	*haveFilter = false;
	if (count_ == 0) return 0;

	// Walk forward over every event due on EXACTLY this tick. FireTick() is the
	// authority on when that is, and the array is sorted by it — see Insert().
	//
	// An exact match, not `<= playHead_`. A "catch up on anything overdue"
	// condition looks safer and is actually wrong here: an event that fires at
	// tick 0 then matches at every tick until the cursor passes it, so it
	// sounds once late on the first pass and again correctly on the next.
	// tools/loopsim.py caught precisely that as a hit firing twice.
	//
	// Exact matching is safe because Advance() cannot skip a tick: the
	// increment is well under one tick even at the top of the tempo range
	// (240bpm gives 4194 of 65536, and the loop inside Advance() would cover
	// larger steps anyway).
	while (cursor_ < count_)
	{
		const LoopEvent &ev = events_[cursor_];
		if (FireTick(ev) != playHead_) break;

		if (ev.what == kFilterEvent)
		{
			*outFilter  = static_cast<int32_t>(ev.value) << 4;
			*haveFilter = true;
		}
		else if (n < kMaxFirePerTick)
		{
			outCombo[n] = static_cast<int8_t>(ev.what);
			outVel[n]   = ev.value;
			n++;
		}
		cursor_++;
	}
	return n;
}

void Looper::Clear()
{
	count_      = 0;
	cursor_     = 0;
	lastFilter_ = -9999;
	// playHead_ and phase_ are left alone: clearing the pattern should not also
	// jump the transport, or an erase mid-performance lurches the timing.
}

} // namespace nib
