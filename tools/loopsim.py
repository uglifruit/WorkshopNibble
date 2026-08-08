#!/usr/bin/env python3
"""loopsim.py - model of the DRUMS event looper.

Mirrors looper.cpp. The interesting properties are not "does it store events"
but the ordering ones, which are easy to get subtly wrong and hard to hear:

  - every recorded hit fires exactly ONCE per pass, never zero or twice;
  - quantisation can move an event EARLIER than the tick it was stored at, and
    such an event must still fire rather than being stranded for a whole loop;
  - overdubbing inserts into a sorted array WHILE a pass is in flight, so the
    playback cursor has to be corrected or the rest of that pass misfires;
  - tempo changes re-time the pattern rather than dropping or doubling hits.

Run:  python tools/loopsim.py
"""

import sys

TICKS_PER_BEAT = 48
BEATS_PER_LOOP = 16
LOOP_TICKS = TICKS_PER_BEAT * BEATS_PER_LOOP      # 768
QUANT_TICKS = TICKS_PER_BEAT // 4                 # 12
NUM_VOICES = 12          # the looper stores VOICES, not key combos
MAX_EVENTS = 512
MAX_FILTER_EVENTS = 128
FILTER_EVENT = 0x80
CTRL_RATE = 3000
BPM_MIN, BPM_MAX = 40, 240
Q16 = 65536

FAILURES = []


def check(name, got, want):
    if got == want:
        print("  PASS  %s" % name)
    else:
        print("  FAIL  %s" % name)
        print("          got:  %r" % (got,))
        print("          want: %r" % (want,))
        FAILURES.append(name)


def fire_tick(ev):
    """When an event actually sounds. Mirrors Looper::FireTick in looper.cpp.

    The event array is sorted by THIS, not by the raw stored tick: quantisation
    can move an event across the loop boundary, and an array sorted by raw tick
    is then not sorted by the order things sound - which double-fires events.
    """
    if ev[1] == FILTER_EVENT:
        return ev[0]
    q = ((ev[0] + QUANT_TICKS // 2) // QUANT_TICKS) * QUANT_TICKS
    return q % LOOP_TICKS


class Looper:
    def __init__(self):
        self.events = []          # list of [tick, what, value], kept sorted
        self.play_head = 0
        self.cursor = 0
        self.phase = 0
        self.tick_inc = 0
        self.last_x = -9999
        self.filter_count = 0

    def set_tempo_bpm(self, bpm):
        self.tick_inc = (bpm * TICKS_PER_BEAT * Q16) // (60 * CTRL_RATE)

    def advance(self):
        if self.tick_inc <= 0:
            return False
        self.phase += self.tick_inc
        if self.phase < Q16:
            return False
        while self.phase >= Q16:
            self.phase -= Q16
            self.play_head = (self.play_head + 1) % LOOP_TICKS
            if self.play_head == 0:
                self.cursor = 0
        return True

    def insert(self, ev):
        if len(self.events) >= MAX_EVENTS:
            return
        ev_when = fire_tick(ev)
        i = len(self.events)
        self.events.append(ev)
        while i > 0 and fire_tick(self.events[i - 1]) > ev_when:
            self.events[i] = self.events[i - 1]
            i -= 1
        self.events[i] = ev
        if ev[1] == FILTER_EVENT:
            self.filter_count += 1
        if i < self.cursor:
            self.cursor += 1

    def remove(self, i):
        if i < 0 or i >= len(self.events):
            return
        if self.events[i][1] == FILTER_EVENT and self.filter_count > 0:
            self.filter_count -= 1
        del self.events[i]
        if i < self.cursor and self.cursor > 0:
            self.cursor -= 1

    def record_filter_at(self, value):
        """Automation REPLACES itself on the same tick rather than piling up."""
        i = 0
        while i < len(self.events):
            if (self.events[i][1] == FILTER_EVENT
                    and fire_tick(self.events[i]) == self.play_head):
                self.remove(i)
            else:
                i += 1
        if self.filter_count >= MAX_FILTER_EVENTS:
            return
        self.insert([self.play_head, FILTER_EVENT, value])

    def record_hit(self, voice, vel=100):
        # A hit the player just performed outranks stale automation.
        if len(self.events) >= MAX_EVENTS and self.filter_count > 0:
            for i, e in enumerate(self.events):
                if e[1] == FILTER_EVENT:
                    self.remove(i)
                    break
        assert 0 <= voice < NUM_VOICES, "loop stores voices, not combos"
        self.insert([self.play_head, voice, vel])

    def fire(self):
        out = []
        while self.cursor < len(self.events):
            ev = self.events[self.cursor]
            if fire_tick(ev) != self.play_head:
                break
            out.append((ev[1], ev[2]))
            self.cursor += 1
        return out

    def clear(self):
        self.events = []
        self.cursor = 0


def run_pass(lp, collect=None):
    """Run exactly one full loop from the current position, collecting fires."""
    fired = []
    start = lp.play_head
    ticks = 0
    while ticks < LOOP_TICKS:
        if lp.advance():
            ticks += 1
            for ev in lp.fire():
                fired.append((lp.play_head, ev[0]))
            if collect is not None:
                collect(lp)
    return fired


def test_every_hit_fires_once():
    """The core property. Record hits across the bar, then play two full passes
    and assert each hit sounds exactly once per pass."""
    lp = Looper()
    lp.set_tempo_bpm(120)

    combos = [0, 1, 2, 3, 4]
    # Place hits at ticks that are NOT already on the quantise grid, so the
    # quantiser genuinely has work to do.
    for i, c in enumerate(combos):
        lp.play_head = 37 + i * 97
        lp.record_hit(c)
    lp.play_head = 0
    lp.cursor = 0

    for p in range(2):
        fired = run_pass(lp)
        got = sorted(c for (_t, c) in fired)
        check("pass %d: every hit fires exactly once" % (p + 1),
              got, sorted(combos))


def test_hit_quantised_earlier_still_fires():
    """A hit stored at tick 5 quantises to 0 - BEFORE where it was recorded.

    Such an event must fire exactly once per pass. Two different bugs live
    here, and this test caught the second:

      - sorting the array by RAW tick leaves it unsorted by the order things
        actually sound, so the cursor walk misfires (fixed by sorting on
        fire_tick);
      - a "catch up on anything overdue" fire condition (`when <= play_head`)
        makes an event that fires at tick 0 match at EVERY tick until the
        cursor passes it, so it sounds late on one pass and again on the next.
        Exact tick matching is correct, and safe because no tick is ever
        skipped at any supported tempo.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 5
    lp.record_hit(7)
    lp.play_head = 0
    lp.cursor = 0

    fired = run_pass(lp)
    check("hit quantised earlier than stored still fires",
          [c for (_t, c) in fired], [7])


def test_overdub_midpass():
    """Insert an event during a pass, at a position the cursor has ALREADY
    walked past. It must not fire twice this pass, and must fire next pass."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 400
    lp.record_hit(1)
    lp.play_head = 0
    lp.cursor = 0

    fired_this = []
    inserted = [False]

    def maybe_insert(l):
        # Once we are past tick 600, add an event back at tick 100 (behind us).
        if not inserted[0] and l.play_head > 600:
            inserted[0] = True
            saved = l.play_head
            l.play_head = 100
            l.record_hit(5)
            l.play_head = saved

    ticks = 0
    while ticks < LOOP_TICKS:
        if lp.advance():
            ticks += 1
            for ev in lp.fire():
                fired_this.append(ev[0])
            maybe_insert(lp)

    check("overdub: event inserted behind cursor does not fire this pass",
          fired_this, [1])

    fired_next = [c for (_t, c) in run_pass(lp)]
    check("overdub: it does fire on the next pass",
          sorted(fired_next), [1, 5])


def test_tempo_retimes_not_drops():
    """Changing tempo must not drop or duplicate hits - the pattern is
    re-timed, which is the whole reason this is an event loop."""
    combos = [0, 2, 4, 6, 8]
    for bpm in (BPM_MIN, 90, 120, 174, BPM_MAX):
        lp = Looper()
        lp.set_tempo_bpm(bpm)
        for i, c in enumerate(combos):
            lp.play_head = i * 150 + 11
            lp.record_hit(c)
        lp.play_head = 0
        lp.cursor = 0
        fired = sorted(c for (_t, c) in run_pass(lp))
        check("tempo %3d: all hits fire once" % bpm, fired, sorted(combos))


def test_tempo_affects_duration():
    """Faster tempo must complete the loop in fewer control ticks."""
    dur = {}
    for bpm in (60, 240):
        lp = Looper()
        lp.set_tempo_bpm(bpm)
        n = 0
        ticks = 0
        while ticks < LOOP_TICKS:
            if lp.advance():
                ticks += 1
            n += 1
        dur[bpm] = n
    check("tempo: 240bpm loop is ~4x shorter than 60bpm",
          abs(dur[60] / dur[240] - 4.0) < 0.05, True)
    print("          60bpm=%d ticks, 240bpm=%d ticks (ratio %.3f)"
          % (dur[60], dur[240], dur[60] / dur[240]))


def test_full_buffer_drops_not_wraps():
    """When the buffer fills, further hits are DROPPED. Wrapping would
    overwrite the oldest events, silently rewriting the pattern."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    for i in range(MAX_EVENTS + 50):
        lp.play_head = i % LOOP_TICKS
        lp.record_hit(i % 10)
    check("full buffer: caps at kMaxEvents", len(lp.events), MAX_EVENTS)


def test_events_stay_sorted():
    """Playback is a cursor walk, which is only valid if the array is sorted."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    import random
    random.seed(7)
    for _ in range(200):
        lp.play_head = random.randrange(LOOP_TICKS)
        lp.record_hit(random.randrange(10))
    ticks = [fire_tick(e) for e in lp.events]
    check("events remain sorted by FIRE time", ticks, sorted(ticks))


def test_automation_cannot_starve_hits():
    """THE OVERDUB BUG.

    Filter automation and drum hits share one array. A continuous knob sweep
    emits an event every kFilterSampleTicks - about 96 per pass - and the first
    version let those ACCUMULATE without limit. After roughly five passes of
    idle twiddling the array was full, Insert() started silently dropping
    everything, and NEW DRUM HITS STOPPED BEING RECORDED.

    From the player's side that is indistinguishable from the looper
    overwriting what they just played, which is exactly how it was reported.

    Two fixes, both checked here: automation replaces itself on a tick rather
    than piling up, and a hit evicts stale automation if the array is full.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)

    # Twenty passes of dense knob movement.
    for _pass in range(20):
        for tick in range(0, LOOP_TICKS, 8):
            lp.play_head = tick
            lp.record_filter_at((tick + _pass) & 0xFF)

    check("automation is capped, not unbounded",
          lp.filter_count <= MAX_FILTER_EVENTS, True)
    print("          after 20 passes of sweeping: %d automation events"
          % lp.filter_count)

    # Now play some hits. Every one must be recorded.
    before = len([e for e in lp.events if e[1] != FILTER_EVENT])
    for i in range(32):
        lp.play_head = i * 20
        lp.record_hit(i % 10)
    after = len([e for e in lp.events if e[1] != FILTER_EVENT])

    check("all 32 hits recorded despite heavy automation",
          after - before, 32)


def test_automation_replaces_on_same_tick():
    """A second knob pass over the same spot should read as 'I redid that
    sweep', not as two sweeps fighting on one tick."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 96
    for v in (10, 20, 30, 40):
        lp.record_filter_at(v)

    at96 = [e for e in lp.events
            if e[1] == FILTER_EVENT and fire_tick(e) == 96]
    check("automation on one tick collapses to the latest", len(at96), 1)
    check("...and it is the most recent value", at96[0][2], 40)


def test_clear_empties():
    lp = Looper()
    lp.set_tempo_bpm(120)
    for i in range(20):
        lp.play_head = i * 30
        lp.record_hit(i % 10)
    lp.clear()
    lp.play_head = 0
    check("clear: no events remain", (len(lp.events), lp.cursor), (0, 0))
    check("clear: nothing fires afterwards",
          [c for (_t, c) in run_pass(lp)], [])


def main():
    print("NIBBLE looper model")
    print()
    test_every_hit_fires_once()
    test_hit_quantised_earlier_still_fires()
    test_overdub_midpass()
    test_tempo_retimes_not_drops()
    test_tempo_affects_duration()
    test_full_buffer_drops_not_wraps()
    test_automation_cannot_starve_hits()
    test_automation_replaces_on_same_tick()
    test_events_stay_sorted()
    test_clear_empties()
    print()
    if FAILURES:
        print("%d FAILED: %s" % (len(FAILURES), ", ".join(FAILURES)))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
