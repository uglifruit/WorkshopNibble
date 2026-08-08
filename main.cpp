// main.cpp — NIBBLE. Card entry point, UI, and output routing.
//
// A program card for the Music Thing Modular Workshop System Computer, built on
// Chris Johnson's header-only ComputerCard library.
//
// NIBBLE reads ONE output of the Workshop System's FOUR VOLTAGES module on
// CV In 1, learns which voltage each button combination produces, and turns the
// combinations into notes (KEYS) or drum hits (DRUMS). See levels.h for the
// ghost rule, which is the piece of this card that cannot be guessed from the
// code around it.
//
// ---------------------------------------------------------------------------
// PANEL
// ---------------------------------------------------------------------------
//
//   CV In 1      Four Voltages output          (the keyboard itself)
//   CV In 2      transpose, 1V/oct             (KEYS only, when patched)
//   Pulse In 1   KEYS: retrigger  /  DRUMS: external clock, 1 edge per beat
//
//   KEYS                                DRUMS
//   Main   macro: envelope length       Main   DJ filter (LP | bypass | HP)
//   X      filter vs loudness split     X      tempo, 40-240bpm (unless clocked)
//   Y      scale                        Y      kit character
//   Switch UP = glide, MID = step       Switch UP = record, MID = play
//   Switch hold 2s = calibrate          Switch hold 2s = calibrate + clear loop
//
//   CV Out 1     1V/oct pitch                  Audio Out 1  drum bus
//   CV Out 2     1V/oct harmony, a third       Audio Out 2  drum bus
//   Audio Out 1  FILTER envelope
//   Audio Out 2  LOUDNESS envelope
//   Pulse Out 1  gate on every note            Pulse Out 1  gate on every hit
//   Pulse Out 2  (unused)                      Pulse Out 2  click, one per beat
//
// The root of the scale sits at 0V, so an oscillator at its own zero is already
// in tune with the card.
//
// In DRUMS a SINGLE button is a shift, not a sound: only pairs trigger. That is
// what lets you hold one button and tap the others to repeat a hit, which is
// what percussion actually needs.
//
// ---------------------------------------------------------------------------
// SINGLE CORE, DELIBERATELY
// ---------------------------------------------------------------------------
//
// There is no multicore_launch_core1() anywhere in this card, and that is a
// decision rather than an omission. The whole per-sample load — one SVF, a
// handful of decaying drum voices, the envelope and CV stage — is a few hundred
// cycles against a budget of 4000 at 192MHz.
//
// WorkshopBio needed a second core only because TinyUSB's tud_task() measured
// at ~36000 cycles, fourteen times the entire budget. NIBBLE speaks no USB, so
// there is nothing to put over there, and launching a core it does not need
// would add a real boot-order hazard for no benefit. Do not cargo-cult the
// multicore setup across from the sibling cards.

#include "ComputerCard.h"

#include "nibble.h"
#include "levels.h"
#include "scales.h"
#include "keys.h"
#include "drums.h"
#include "looper.h"
#include "fastmath.h"

#include "hardware/vreg.h"
#include "pico/stdlib.h"

using namespace nib;

namespace {

// ---------------------------------------------------------------------------
// Learn-mode constants
// ---------------------------------------------------------------------------

/// Ergonomic learn order: singles, then rows, columns, diagonals.
///
/// Deliberately NOT index order. The hand alternates shape instead of
/// re-forming similar grips, and on the 2x2 LED block it draws a geometric
/// sequence — top row, bottom row, left column, right column, then the two
/// diagonals — which the player reads off the panel without counting or
/// memorising anything.
constexpr uint8_t kLearnOrder[kNumLevels] = { kA, kB, kC, kD,
                                              kAB, kCD, kAC, kBD, kAD, kBC };

/// Abandon a learn that has stalled. Generous, because the learn is self-paced
/// now that capture is tap-confirmed — a player may well be re-reading the
/// panel. Its only job is to stop a card being left permanently in learn.
constexpr int32_t kLearnTimeoutTicks = 30 * kCtrlRate;

constexpr int32_t kCaptureFlashTicks   = kCtrlRate / 5;    // 200ms
constexpr int32_t kCollisionFlashTicks = kCtrlRate / 2;    // ~450ms
constexpr int32_t kDoneFlashTicks      = (kCtrlRate * 2) / 5;
constexpr int32_t kScaleShowTicks      = (kCtrlRate * 6) / 5;   // 1.2s

/// Gate length for note events, in samples. 5ms is long enough for anything
/// downstream to see it and short enough not to smear fast playing.
constexpr int32_t kGateSamples = kSampleRate / 200;

/// Click-track pulse length. Shorter than a gate — it is a metronome tick, not
/// a note, and at 240bpm the beats are only a quarter of a second apart.
constexpr int32_t kClickSamples = kSampleRate / 500;   // 2ms

// ---------------------------------------------------------------------------

enum class UiMode : uint8_t { Play, Learn };

/// What the learn machine is doing between captures.
enum class LearnPhase : uint8_t { Waiting, Confirm, Collision, Done, Failed, Aborted };

/// A learn is REJECTED outright if the ten captured levels do not span at least
/// this much of the input range.
///
/// The case this exists for is calibrating with nothing patched into CV In 1:
/// every step captures the same floating value, the card cheerfully builds a
/// table of ten identical levels, and then plays one note forever. That looked
/// like a working calibration and a broken card.
///
/// 400 units is about 1.2V. Ten levels genuinely spread across a Four Voltages
/// output cover several volts, so this rejects only the degenerate cases —
/// nothing patched, a dead cable, or every button read as the same voltage.
constexpr int32_t kMinLearnSpan = 400;

/// How long the failure is announced before dropping back to play.
constexpr int32_t kFailFlashTicks = 3 * kCtrlRate / 2;   // 1.5s

} // namespace

// ===========================================================================

class NibbleCard : public ComputerCard
{
public:
	NibbleCard()
	{
		// NOTHING that touches hardware may happen here. This constructor runs
		// during ComputerCard's own construction, before Run() has set the
		// peripherals up, and heavy work here wedges the chip — a lesson paid
		// for in both sibling cards. Plain field initialisation only.
		levels_.InitDefault();
	}

	virtual void __not_in_flash_func(ProcessSample)() override
	{
		// ---- Boot window ------------------------------------------------
		//
		// The switch is NOT readable straight away, and reads Down until it
		// settles: ComputerCard derives it from knobs[3], off a ~60Hz smoothing
		// filter starting at zero, and zero decodes as Down. So for the first
		// few milliseconds of EVERY boot the card reports Down wherever the
		// switch actually is.
		//
		// Latching on "Down seen at any point in the window" therefore latches
		// on every boot — both WorkshopZX and WorkshopBio shipped exactly that
		// bug. Take ONE reading once settled instead.
		if (bootPhase_ < kBootWindowSamples)
		{
			if (++bootPhase_ == kBootWindowSamples)
			{
				boot_ = (SwitchVal() == Switch::Down) ? BootMode::Drums
				                                      : BootMode::Keys;

				// Swallow the release of the boot hold, so booting into DRUMS
				// does not immediately read as a tap and fire a retrigger.
				holdFired_ = (SwitchVal() == Switch::Down);

				splash_ = kSplashSamples;
			}
			return;
		}

		// ---- Boot splash -------------------------------------------------
		// KEYS lights the left column, DRUMS the right, matching WorkshopBio's
		// convention so the cards feel like siblings.
		if (splash_ > 0)
		{
			if (--splash_ == 0)
			{
				for (int i = 0; i < kNumLeds; i++) LedOff(i);
			}
			else
			{
				bool drums = (boot_ == BootMode::Drums);
				// An uncalibrated CV out cannot track 1V/oct, and the player
				// should learn that from the card rather than by blaming their
				// own learn pass. Blink the splash instead of holding it.
				bool show = CVOutsCalibrated()
				          || ((splash_ >> 12) & 1);
				for (int i = 0; i < kNumLeds; i++)
					LedOn(i, show && (((i & 1) == 1) == drums));
			}
		}

		// ---- Control tick ------------------------------------------------
		//
		// NOTE: this runs INLINE, inside a DMA interrupt handler. The divider
		// lowers the AVERAGE load but does NOT relax the deadline: on the
		// sample where it fires, everything below must still finish within this
		// one 20.83us slot.
		if (++ctrlDiv_ >= kCtrlDiv) ctrlDiv_ = 0;
		if (ctrlDiv_ == 0)      ControlTick();
		else if (ctrlDiv_ == 8) UiTick();

		// ---- Audio rate --------------------------------------------------
		if (boot_ == BootMode::Keys) AudioKeys();
		else                         AudioDrums();

		if (gateTimer_ > 0)
		{
			gateTimer_--;
			PulseOut1(true);
		}
		else
		{
			PulseOut1(false);
		}

		// Pulse Out 2: the DRUMS click track, silent in KEYS.
		if (clickTimer_ > 0)
		{
			clickTimer_--;
			PulseOut2(true);
		}
		else
		{
			PulseOut2(false);
		}
	}

private:
	// =======================================================================
	// Control rate
	// =======================================================================

	void __not_in_flash_func(ControlTick)()
	{
		ReadSwitch();

		// Pulse In 1 does different jobs in the two modes.
		//
		// KEYS: retrigger the held note — this is how you play repeated notes.
		// DRUMS: an external CLOCK, one edge per quarter note, which overrides
		// the X knob for as long as it keeps arriving. A drum looper wants to
		// follow the rest of the rack far more than it wants a second way to
		// re-fire a hit.
		if (PulseIn1RisingEdge())
		{
			if (boot_ == BootMode::Drums) loop_.ClockPulse();
			else                          Retrigger();
		}

		if (ui_ == UiMode::Learn) { LearnTick(); return; }

		// --- level detection ---
		int8_t idx = kComboNone;
		LevelEvent ev = levels_.Step(CVIn1(), idx);
		if (ev == LevelEvent::Trigger) FireCombo(idx);

		if (boot_ == BootMode::Keys) KeysControl();
		else                          DrumsControl();
	}

	/// Tap vs hold on the momentary switch.
	///
	/// The same control carries four actions across two contexts:
	///   Play  + tap  -> retrigger      Play  + hold -> enter learn
	///   Learn + tap  -> capture step   Learn + hold -> abort learn
	///
	/// The action fires on RELEASE for a tap and on reaching the count for a
	/// hold. That split is what lets both coexist: beginning a hold must not
	/// also fire the tap.
	void __not_in_flash_func(ReadSwitch)()
	{
		Switch sw = SwitchVal();
		tapped_ = false;

		if (sw == Switch::Down)
		{
			// Do not start counting until the splash has finished AND the
			// switch has been released at least once. Without the second
			// condition, holding at power-on to select DRUMS and simply not
			// letting go fast enough would drop you straight into learn.
			if (splash_ == 0 && !holdFired_ && downTicks_ < kHoldTicks)
				downTicks_++;

			if (downTicks_ >= kHoldTicks && !holdFired_)
			{
				holdFired_ = true;
				if (ui_ == UiMode::Learn) AbortLearn();
				else                      EnterLearn();
			}
		}
		else
		{
			if (downTicks_ > 0 && !holdFired_) tapped_ = true;
			downTicks_ = 0;
			holdFired_ = false;
		}

		if (ui_ == UiMode::Play && tapped_) Retrigger();
	}

	void Retrigger()
	{
		if (ui_ == UiMode::Learn) return;

		if (boot_ == BootMode::Keys)
		{
			keys_.Retrigger();
			gateTimer_ = kGateSamples;
		}
		else
		{
			// Singles are shifts, not sounds — a retrigger while only a shift is
			// held has nothing to re-fire. (See FireCombo.)
			int8_t cur = levels_.Current();
			if (cur >= kNumSingles)
			{
				drums_.Trigger(cur, KnobVal(Knob::Y));
				if (recording_) loop_.RecordHit(cur, 100);
				gateTimer_ = kGateSamples;
				ledFlash_[cur & 3] = kCtrlRate / 8;
			}
		}
	}

	/// A genuine new press, from the level detector or the loop.
	void __not_in_flash_func(FireCombo)(int8_t combo)
	{
		if (combo < 0 || combo >= kNumLevels) return;

		// In DRUMS a single button is a SHIFT, not a sound.
		//
		// Percussion wants repeated hits on the same instrument, and that is
		// exactly what a keyboard reading of the buttons cannot give you: to
		// play AC twice you must pass through C, and if C is itself a sound
		// then every repeat is interrupted by a spurious one. Making the four
		// singles silent turns them into bank-selects that can be HELD for as
		// long as you like — hold C and tap A, B or D, over and over.
		//
		// Suppressed here at the output rather than in the detector: KEYS still
		// plays its singles, and the tracker's state (including the ghost) has
		// to keep working identically in both modes.
		if (boot_ == BootMode::Drums && combo < kNumSingles)
		{
			// Still worth showing on the LEDs — knowing which shift is held is
			// the whole point of the gesture.
			return;
		}

		if (boot_ == BootMode::Keys)
		{
			int root = kBaseNote;
			// CV In 2 transposes, but ONLY when something is actually patched:
			// an open jack floats around zero and would detune the keyboard at
			// random. EnableNormalisationProbe() makes Connected() meaningful.
			if (Connected(Input::CV2))
			{
				// +/-2048 spans +/-6V, so 1V is ~341 units and a semitone ~28.4.
				// Rounded, and quantised to whole semitones so an imprecise
				// voltage cannot leave the whole keyboard slightly sharp.
				int32_t semis = (CVIn2() * 12 + 170) / 341;
				if (semis >  kTransposeSemis) semis =  kTransposeSemis;
				if (semis < -kTransposeSemis) semis = -kTransposeSemis;
				root += semis;
			}
			// The root is 0V, so a negative transpose has nowhere useful to go:
			// the DAC can swing below zero but an oscillator's 1V/oct input
			// generally cannot use it.
			if (root < 0) root = 0;

			// Cap so the HIGHEST note the card can produce still fits the ~6V
			// output. The widest case is the Maj7 arpeggio, whose harmony voice
			// reaches 31 semitones above the root; 40 + 31 = 71 semitones is
			// 5.92V, just inside. Anything higher would clip silently.
			if (root > kMaxRoot) root = kMaxRoot;

			// The harmony is a few degrees up, run through the SAME scale — so
			// it is a third that lands major or minor according to where in the
			// scale you are, and it follows the Y knob for free rather than
			// being a fixed interval bolted on.
			keys_.NoteOn(QuantizeNote(root, scaleIdx_, combo),
			             QuantizeNote(root, scaleIdx_,
			                          combo + HarmonyDegreesFor(scaleIdx_)));
		}
		else
		{
			drums_.Trigger(combo, KnobVal(Knob::Y));
			if (recording_) loop_.RecordHit(combo, 100);
		}

		gateTimer_ = kGateSamples;
		ledFlash_[combo & 3] = kCtrlRate / 8;
	}

	// --- KEYS ---------------------------------------------------------

	void __not_in_flash_func(KeysControl)()
	{
		keys_.SetMacro(KnobVal(Knob::Main), KnobVal(Knob::X));

		int32_t y = KnobVal(Knob::Y);
		scaleIdx_ = (y * kNumScales) >> 12;
		if (scaleIdx_ >= kNumScales) scaleIdx_ = kNumScales - 1;

		int32_t dy = y - yLast_;
		if (dy > kKnobMoveThresh || dy < -kKnobMoveThresh)
		{
			yLast_ = y;
			scaleShow_ = kScaleShowTicks;
		}
		if (scaleShow_ > 0) scaleShow_--;

		glide_ = (SwitchVal() == Switch::Up);
	}

	// --- DRUMS --------------------------------------------------------

	void __not_in_flash_func(DrumsControl)()
	{
		Switch sw = SwitchVal();
		recording_ = (sw == Switch::Up);

		loop_.SetTempo(KnobVal(Knob::X));
		djFilter_.SetKnob(filterOverride_ >= 0 ? filterOverride_
		                                       : KnobVal(Knob::Main));

		// Erasing the loop is done by entering calibration — see EnterLearn().
		// The old gesture here was "hold the switch UP with no hits played",
		// which cannot survive singles becoming SHIFTS: a shift is held for
		// long stretches while playing, so "no hits played" is true far more
		// often than the player means it to be.

		if (recording_) loop_.RecordFilter(KnobVal(Knob::Main));

		if (loop_.Advance())
		{
			// Pulse Out 2 is a CLICK TRACK: one blip per crotchet, running
			// whenever the loop is, so you have something to record along to.
			// Patch it at a click voice, or just watch LED 4.
			//
			// Driven from BeatEdge() rather than OnBeat(): the latter is a level
			// that stays true for the whole tick, which at 40bpm is dozens of
			// control steps — a click that is on more than it is off.
			if (loop_.BeatEdge()) clickTimer_ = kClickSamples;

			int8_t  combo[Looper::kMaxFirePerTick];
			uint8_t vel[Looper::kMaxFirePerTick];
			int32_t filt = 0;
			bool    haveFilt = false;

			int n = loop_.Fire(combo, vel, &filt, &haveFilt);
			for (int i = 0; i < n; i++)
			{
				drums_.Trigger(combo[i], KnobVal(Knob::Y));
				gateTimer_ = kGateSamples;
				ledFlash_[combo[i] & 3] = kCtrlRate / 8;
			}
			// Recorded filter automation takes over the knob until the player
			// moves it again — otherwise the live knob position would fight
			// the playback on every tick.
			if (haveFilt && !recording_) filterOverride_ = filt;
		}

		// Moving the knob reclaims it from the recorded automation.
		int32_t m = KnobVal(Knob::Main);
		int32_t dm = m - mainLast_;
		if (dm > kKnobMoveThresh || dm < -kKnobMoveThresh)
		{
			mainLast_ = m;
			filterOverride_ = -1;
		}
	}

	// =======================================================================
	// Learn
	// =======================================================================

	void EnterLearn()
	{
		ui_          = UiMode::Learn;
		learnStep_   = 0;
		learnPhase_  = LearnPhase::Waiting;
		collisionsThisLearn_ = 0;
		learnTimer_  = kLearnTimeoutTicks;
		phaseTimer_  = 0;

		// Entering calibration CLEARS the loop.
		//
		// This is the erase gesture, and it is the only one that makes sense
		// here. Every switch position and every button is already spoken for
		// while playing — in particular the four singles are SHIFTS that get
		// held for long stretches, so "hold a button to erase" would fire
		// constantly. Recalibrating is also the one moment where a stale loop
		// is guaranteed to be meaningless: the levels are about to change, so
		// the combos the pattern refers to may not even exist afterwards.
		loop_.Clear();
		filterOverride_ = -1;
		// Entering on a HOLD means the release of that hold is still to come.
		// holdFired_ is already true here, which swallows it — without that,
		// the release would immediately read as the capture tap for step 0.
	}

	/// Discard the learn and keep whatever calibration was there before.
	///
	/// Note this stays in UiMode::Learn until the flash has played out, and only
	/// then returns to Play. Setting ui_ = Play here instead looks equivalent
	/// and is not: UiTick() would immediately take its Play branch, LearnLeds()
	/// would never render the Aborted phase, and the "your learn was thrown
	/// away" flash — the only feedback that the abort worked — would be dead
	/// code that never lit an LED.
	void AbortLearn()
	{
		learnPhase_ = LearnPhase::Aborted;
		phaseTimer_ = kCaptureFlashTicks * 2;
	}

	void __not_in_flash_func(LearnTick)()
	{
		// Keep the detector running so Settled()/SettledValue() are live.
		int8_t dummy = kComboNone;
		(void)levels_.Step(CVIn1(), dummy);

		if (phaseTimer_ > 0)
		{
			if (--phaseTimer_ == 0 && learnPhase_ != LearnPhase::Waiting)
			{
				// Done and Aborted both END the learn; Confirm and Collision are
				// just per-step feedback and fall back to waiting for the next
				// capture.
				if (learnPhase_ == LearnPhase::Done
				 || learnPhase_ == LearnPhase::Failed
				 || learnPhase_ == LearnPhase::Aborted)
				{
					// Drop whatever combo the learn left "held", or the first
					// real press could match it and be swallowed as no-change.
					levels_.ResetHeld();
					ui_ = UiMode::Play;
				}
				else
				{
					learnPhase_ = LearnPhase::Waiting;
					learnTimer_ = kLearnTimeoutTicks;
				}
			}
			return;
		}

		if (--learnTimer_ <= 0) { AbortLearn(); return; }

		if (!tapped_) return;

		// A tap that arrived mid-transition would capture a voltage the player
		// is not actually holding. Reject it and say so, rather than silently
		// recording a number from the middle of a slew.
		if (!levels_.Settled())
		{
			learnPhase_ = LearnPhase::Collision;    // reuses the 4+5 flash
			phaseTimer_ = kCaptureFlashTicks;
			return;
		}

		captured_[kLearnOrder[learnStep_]] = levels_.SettledValue();

		// Warn if this level is too close to one already taken. The capture is
		// still KEPT: a learn that completes with a warning is far more useful
		// than one that refuses, and the player can move the Four Voltages knob
		// and run it again.
		bool collided = false;
		for (int j = 0; j < learnStep_; j++)
		{
			int32_t d = captured_[kLearnOrder[learnStep_]]
			          - captured_[kLearnOrder[j]];
			if (d < 0) d = -d;
			if (d < kCollisionMin) collided = true;
		}
		if (collided) collisionsThisLearn_++;

		learnStep_++;
		if (learnStep_ >= kNumLevels)
		{
			// Refuse a degenerate calibration rather than installing it.
			// Ten captures that all landed on the same voltage means nothing
			// was patched in (or the cable is dead) — accepting that would give
			// a card that looks calibrated and plays one note forever.
			int32_t lo = captured_[0], hi = captured_[0];
			for (int i = 1; i < kNumLevels; i++)
			{
				if (captured_[i] < lo) lo = captured_[i];
				if (captured_[i] > hi) hi = captured_[i];
			}

			if (hi - lo < kMinLearnSpan)
			{
				// Keep whatever calibration was there before.
				learnPhase_ = LearnPhase::Failed;
				phaseTimer_ = kFailFlashTicks;
			}
			else
			{
				levels_.LearnFrom(captured_);
				learnPhase_ = LearnPhase::Done;
				phaseTimer_ = kDoneFlashTicks;
			}
		}
		else
		{
			learnPhase_ = collided ? LearnPhase::Collision : LearnPhase::Confirm;
			phaseTimer_ = collided ? kCollisionFlashTicks : kCaptureFlashTicks;
		}
	}

	// =======================================================================
	// Audio rate
	// =======================================================================

	void __not_in_flash_func(AudioKeys)()
	{
		int32_t env[kNumEnvs];
		keys_.StepEnvelopes(env);

		// The audio outs are DC-coupled, so they carry CV happily — but they
		// are not calibrated the way the CV outs are. Fine for driving envelope
		// and filter inputs; not accurate enough for pitch.
		AudioOut1(clamp12(env[kEnvFilter]));
		AudioOut2(clamp12(env[kEnvLoudness]));

		// CVOutMillivolts calls MillivoltsToDAC, which is FLASH-RESIDENT in the
		// vendored ComputerCard. Calling it every sample at 48kHz would put XIP
		// reads in the hot loop for a value that mostly has not changed, so
		// both CV outs are cached and only written on a change.
		int32_t pitchMv = keys_.PitchMillivolts(glide_);
		if (pitchMv != cvLastMv_[0])
		{
			cvLastMv_[0] = pitchMv;
			CVOutMillivolts(0, pitchMv);
		}

		// CV Out 2 is the HARMONY voice, a third above in the current scale —
		// it used to be a third envelope, which turned out to be saying the
		// same thing as the loudness one.
		int32_t harmMv = keys_.HarmonyMillivolts(glide_);
		if (harmMv != cvLastMv_[1])
		{
			cvLastMv_[1] = harmMv;
			CVOutMillivolts(1, harmMv);
		}
	}

	void __not_in_flash_func(AudioDrums)()
	{
		int32_t dry = drums_.Step();
		int32_t wet = djFilter_.Step(dry);
		int16_t out = clamp12(wet);

		// Mono bus to both outs: the kit is not panned, and a player patching
		// one output should get the whole kit rather than half of it.
		AudioOut1(out);
		AudioOut2(out);
	}

	// =======================================================================
	// LEDs
	// =======================================================================

	void __not_in_flash_func(UiTick)()
	{
		if (splash_ > 0) return;

		for (int i = 0; i < 4; i++)
			if (ledFlash_[i] > 0) ledFlash_[i]--;

		if (ui_ == UiMode::Learn) { LearnLeds(); return; }

		if (boot_ == BootMode::Keys && scaleShow_ > 0) { ScaleLeds(); return; }

		// --- normal play ---
		// LEDs 0-3 mirror the Four Voltages A B / C D buttons: the top 2x2 of
		// the Computer's block sits in the same arrangement, which is the best
		// affordance available and is used consistently everywhere on this card.
		// Show the SOUNDING combo, not the tracker's raw current level.
		//
		// While a ghost is armed those differ: the CV has fallen back to one of
		// the released pair's members, so Current() reports that single — but
		// the pair is still what you can hear, and showing the single made the
		// display contradict the sound on every release. Sounding() reports the
		// pair for as long as the ghost holds.
		uint8_t mask = ComboLedMask(levels_.Sounding());
		for (int i = 0; i < 4; i++)
		{
			uint16_t b = (mask & (1u << i)) ? kLedDim : 0;
			if (ledFlash_[i] > 0) b = kLedFull;
			LedBrightness(i, b);
		}

		if (boot_ == BootMode::Keys)
		{
			// LEDs 4 and 5 stay DARK while playing, deliberately.
			//
			// LED 4 used to flash on every note, which is information LEDs 0-3
			// already carry — the combo lights up as you play it, and a second
			// light saying "a note happened" only competes with it. LED 5 used
			// to sit on permanently to mean "calibrated", which is not something
			// you can act on mid-performance and just added a light to ignore.
			//
			// Both still have jobs elsewhere: during calibration they are the
			// phase markers, and LED 4 is the beat in DRUMS. Here there is
			// nothing worth saying, so they say nothing.
			LedOff(4);
			LedOff(5);
		}
		else
		{
			LedBrightness(4, loop_.OnBeat() ? kLedFull : kLedGlow);
			LedOn(5, recording_);
		}
		uiTicks_++;
	}

	void __not_in_flash_func(LearnLeds)()
	{
		switch (learnPhase_)
		{
		case LearnPhase::Confirm:
			for (int i = 0; i < kNumLeds; i++) LedOn(i, true);
			return;

		case LearnPhase::Collision:
			// Only the phase markers flash, so a collision reads differently
			// from a clean capture without stopping the learn.
			for (int i = 0; i < 4; i++) LedOff(i);
			LedOn(4, ((phaseTimer_ >> 5) & 1) != 0);
			LedOn(5, ((phaseTimer_ >> 5) & 1) != 0);
			return;

		case LearnPhase::Done:
		{
			// A clean learn ramps all six up and fades. A learn that completed
			// but recorded collisions ramps the same way with LEDs 4 and 5
			// flashing over the top — so the warning is delivered ONCE, here,
			// where you can act on it, instead of blinking at you forever
			// afterwards while you play.
			uint16_t b = static_cast<uint16_t>(
				kLedFull - (phaseTimer_ * kLedFull) / kDoneFlashTicks);
			for (int i = 0; i < 4; i++) LedBrightness(i, b);

			if (collisionsThisLearn_)
			{
				bool f = ((phaseTimer_ >> 5) & 1) != 0;
				LedBrightness(4, f ? kLedFull : 0);
				LedBrightness(5, f ? kLedFull : 0);
			}
			else
			{
				LedBrightness(4, b);
				LedBrightness(5, b);
			}
			return;
		}

		case LearnPhase::Failed:
			// Nothing usable came in — almost always nothing patched into
			// CV In 1. An urgent, unmistakably different pattern: the two
			// COLUMNS alternating, fast. Not the gentle fade of a success and
			// not the double blink of a deliberate abort.
			for (int i = 0; i < kNumLeds; i++)
				LedOn(i, (((phaseTimer_ >> 4) & 1) != 0) == ((i & 1) == 0));
			return;

		case LearnPhase::Aborted:
			for (int i = 0; i < kNumLeds; i++)
				LedOn(i, ((phaseTimer_ >> 6) & 1) != 0);
			return;

		case LearnPhase::Waiting:
		default:
			break;
		}

		// Waiting for the player to hold a combo and tap to confirm.
		uint8_t want = ComboLedMask(static_cast<int8_t>(kLearnOrder[learnStep_]));
		bool    blink = ((uiTicks_ >> 7) & 1) != 0;

		for (int i = 0; i < 4; i++)
		{
			if (want & (1u << i))       LedBrightness(i, blink ? kLedFull : kLedDim);
			else if (Captured(i))       LedBrightness(i, kLedGlow);
			else                        LedOff(i);
		}

		// Phase marker: LED 4 during the four singles, LED 5 during the pairs.
		bool pairs = (learnStep_ >= kNumSingles);
		LedBrightness(4, pairs ? 0 : kLedDim);
		LedBrightness(5, pairs ? kLedDim : 0);

		uiTicks_++;
	}

	/// Has button `b` appeared in any combo captured so far? Used only to give
	/// already-visited buttons a dim glow during the singles phase.
	bool Captured(int b) const
	{
		for (int s = 0; s < learnStep_; s++)
			if (ComboLedMask(static_cast<int8_t>(kLearnOrder[s])) & (1u << b))
				return true;
		return false;
	}

	void __not_in_flash_func(ScaleLeds)()
	{
		// Twelve scales on six LEDs, as a level meter rather than a binary
		// count: binary is unreadable at a glance with both hands busy. LEDs
		// light cumulatively bottom-to-top, left column then right, and the
		// second six repeat at half brightness — so "more light" tracks the
		// table's dark-to-bright ordering.
		static constexpr uint8_t kBarOrder[6] = { 4, 2, 0, 5, 3, 1 };
		int lit  = (scaleIdx_ % 6) + 1;
		bool half = (scaleIdx_ >= 6);

		for (int i = 0; i < 6; i++)
		{
			int pos = 0;
			while (pos < 6 && kBarOrder[pos] != i) pos++;
			LedBrightness(i, (pos < lit) ? (half ? kLedDim : kLedFull) : 0);
		}
	}

	// =======================================================================
	// State
	// =======================================================================

	LevelTracker levels_;
	Keys         keys_;
	DrumKit      drums_;
	DjFilter     djFilter_;
	Looper       loop_;

	BootMode boot_       = BootMode::Keys;
	UiMode   ui_         = UiMode::Play;
	int32_t  bootPhase_  = 0;
	int32_t  splash_     = 0;
	int32_t  ctrlDiv_    = 0;
	uint32_t uiTicks_    = 0;

	// switch
	int32_t downTicks_ = 0;
	bool    holdFired_ = false;
	bool    tapped_    = false;

	// learn
	int32_t    captured_[kNumLevels] = {};
	int        learnStep_  = 0;
	LearnPhase learnPhase_ = LearnPhase::Waiting;
	int32_t    learnTimer_ = 0;
	int32_t    phaseTimer_ = 0;
	uint8_t    collisionsThisLearn_ = 0;

	// keys
	int      scaleIdx_  = 8;          // Ionian by default
	int32_t  yLast_     = -9999;
	int32_t  scaleShow_ = 0;
	bool     glide_     = false;

	// drums
	bool    recording_      = false;
	int32_t filterOverride_ = -1;
	int32_t mainLast_       = -9999;

	// outputs
	int32_t gateTimer_    = 0;
	int32_t clickTimer_   = 0;
	int32_t cvLastMv_[2]  = { -999999, -999999 };
	int32_t ledFlash_[4]  = {};
};

// ===========================================================================

int main()
{
	// Overclock before anything else. 192MHz at 1.15V is proven on this exact
	// hardware by the sibling cards; the brief settle after the voltage change
	// is what stops the PLL relock from landing on an unstable rail.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	sleep_ms(2);
	set_sys_clock_khz(192000, true);

	static NibbleCard card;

	// Makes Connected()/Disconnected() meaningful, which the CV In 2 transpose
	// depends on: an unpatched jack floats and would otherwise transpose the
	// keyboard at random.
	card.EnableNormalisationProbe();

	card.Run();   // never returns
}
