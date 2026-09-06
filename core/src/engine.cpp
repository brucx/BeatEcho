#include "beat_echo/engine.hpp"
namespace beat_echo {
namespace {
int clamp(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
Time abs_time(Time t) { return t < 0 ? -t : t; }
Settings normalized(Settings s) {
    s.bpm = clamp(s.bpm, 60, 160);
    s.difficulty = clamp(s.difficulty, 0, 2);
    s.input_offset_ms = clamp(s.input_offset_ms, -200, 200);
    return s;
}
}
uint32_t random_step(uint32_t& state) {
    if (!state) state = 0x6d2b79f5U;
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}
void Engine::reset(Settings settings, uint32_t seed) {
    settings_ = normalized(settings); seed_ = seed ? seed : 1;
    phase_ = Phase::Menu; schedule_ = {}; note_count_ = 0;
    round_ = 1; lives_ = 3; bars_ = 1; stats_ = {};
    session_score_ = best_score_ = 0; won_ = false;
    count_start_ = listen_start_ = ready_start_ = repeat_start_ = end_at_ = 0;
    duration_ = last_tick_ = 0;
}
bool Engine::configure(Settings s) {
    if (phase_ != Phase::Menu && phase_ != Phase::GameOver) return false;
    settings_ = normalized(s);
    return true;
}
bool Engine::active() const {
    return phase_ == Phase::CountIn || phase_ == Phase::Listen ||
           phase_ == Phase::Ready || phase_ == Phase::Repeat;
}
void Engine::abort() {
    phase_ = Phase::Menu; ++schedule_.epoch; schedule_.count = 0;
}
void Engine::select(Time now) {
    if (now < 0) return;
    if (phase_ == Phase::Menu || phase_ == Phase::GameOver) {
        round_ = 1; lives_ = 3; session_score_ = 0; won_ = false;
        begin_round(now);
    } else if (phase_ == Phase::Result) {
        if (stats_.passed) ++round_;
        begin_round(now); // Same seed + round on failure: repeat the same phrase.
    }
}
void Engine::add_note(Time at, unsigned key) {
    if (note_count_ < kMaxNotes && key < kKeys)
        notes_[note_count_++] = {at, static_cast<uint8_t>(key), Grade::Pending};
}
void Engine::generate_pattern() {
    note_count_ = 0;
    const unsigned difficulty = static_cast<unsigned>(settings_.difficulty);
    bars_ = (difficulty == 2 && round_ >= 4) || (difficulty == 1 && round_ >= 7) ? 2 : 1;
    const unsigned subdivisions = difficulty == 0 ? 1 : (difficulty == 1 ? 2 : 4);
    const unsigned steps = bars_ * 4 * subdivisions;
    duration_ = beat_us() * 4 * bars_;
    uint32_t rng = seed_ ^ (round_ * 0x9e3779b9U) ^ (difficulty * 0x85ebca6bU);
    for (unsigned step = 0; step < steps; ++step) {
        // Anchor each bar on kick/snare. Rests and subdivisions remain memorable.
        const bool anchor = step % (4 * subdivisions) == 0 || step % (4 * subdivisions) == 2 * subdivisions;
        const unsigned density = difficulty == 0 ? 45 : 45 + (round_ > 8 ? 8 : round_) * 2;
        if (!anchor && random_step(rng) % 100 >= density) continue;
        unsigned key = step % (4 * subdivisions) == 0 ? 0 : 1;
        const unsigned palette = difficulty == 0 || (difficulty == 1 && round_ <= 2) ? 4 : 8;
        if (!anchor) key = random_step(rng) % palette;
        // Divide the entire phrase once: no repeated rounded sleep/BPM accumulation.
        const Time at = duration_ * step / steps;
        add_note(at, key);
        if (difficulty == 2 && round_ >= 3 && random_step(rng) % 100 < 22) {
            const unsigned second = (key + 1 + random_step(rng) % 7) % 8;
            add_note(at, second);
        }
    }
}
void Engine::add_cue(Time at, unsigned voice, unsigned velocity) {
    if (schedule_.count >= kMaxCues) return; // Capacity proven in host tests.
    unsigned i = schedule_.count++;
    while (i && schedule_.cues[i - 1].at > at) {
        schedule_.cues[i] = schedule_.cues[i - 1]; --i;
    }
    schedule_.cues[i] = {at, static_cast<uint8_t>(voice), static_cast<uint8_t>(velocity)};
}
void Engine::begin_round(Time now) {
    stats_ = {}; generate_pattern();
    ++schedule_.epoch; schedule_.count = 0;
    // Quarter-second headroom allows both adapters to preload their audio pipeline.
    count_start_ = now + 250000;
    listen_start_ = count_start_ + 4 * beat_us();
    ready_start_ = listen_start_ + duration_;
    repeat_start_ = ready_start_ + 4 * beat_us();
    // Offset affects the delivery horizon too, without changing the +/-150ms window.
    const Time positive_offset = settings_.input_offset_ms > 0 ? settings_.input_offset_ms * 1000LL : 0;
    end_at_ = repeat_start_ + duration_ + windows_.bad + kJudgementGraceUs + positive_offset;
    last_tick_ = now; last_combo_at_ = repeat_start_ - windows_.bad - 1; phase_ = Phase::CountIn;
    for (unsigned i = 0; i < 4; ++i) {
        add_cue(count_start_ + i * beat_us(), 8, i == 0 ? 90 : 65);
        add_cue(ready_start_ + i * beat_us(), 8, i == 0 ? 90 : 65);
    }
    for (unsigned i = 0; i < bars_ * 4; ++i) {
        add_cue(listen_start_ + i * beat_us(), 8, 20);
        add_cue(repeat_start_ + i * beat_us(), 8, 25);
    }
    for (unsigned i = 0; i < note_count_; ++i)
        add_cue(listen_start_ + notes_[i].at, notes_[i].key, 100);
}
void Engine::update_score() {
    const int earned = static_cast<int>(stats_.perfect * 1000 + stats_.good * 700 + stats_.bad * 300);
    const int penalty = static_cast<int>(stats_.stray * 100);
    stats_.score = earned > penalty ? earned - penalty : 0;
    stats_.accuracy = note_count_ ? stats_.score / static_cast<int>(note_count_) : 0;
}
void Engine::finish() {
    for (unsigned i = 0; i < note_count_; ++i) {
        if (notes_[i].grade == Grade::Pending) {
            notes_[i].grade = Grade::Miss; ++stats_.miss; stats_.combo = 0;
        }
    }
    update_score();
    const int threshold = 600 + settings_.difficulty * 100;
    stats_.passed = stats_.accuracy >= threshold;
    if (stats_.passed) session_score_ += stats_.score;
    else if (lives_) --lives_;
    if (session_score_ > best_score_) best_score_ = session_score_;
    won_ = stats_.passed && round_ >= kFinalRound;
    phase_ = (!lives_ || won_) ? Phase::GameOver : Phase::Result;
}
void Engine::tick(Time now) {
    if (!active() || now < last_tick_) return;
    last_tick_ = now;
    if (now >= end_at_) { finish(); return; }
    phase_ = now >= repeat_start_ ? Phase::Repeat :
             now >= ready_start_ ? Phase::Ready :
             now >= listen_start_ ? Phase::Listen : Phase::CountIn;
    // Misses are finalized at phrase end. This avoids throwing away a captured
    // edge because a task delivered it late, and makes chords order-independent.
}
Grade Engine::press(unsigned key, Time captured_at) {
    if (key >= kKeys || !active()) return Grade::Ignored;
    const Time at = captured_at - settings_.input_offset_ms * 1000LL;
    if (at < repeat_start_ - windows_.bad || at > repeat_start_ + duration_ + windows_.bad)
        return Grade::Ignored;
    unsigned best = kMaxNotes; Time closest = windows_.bad + 1;
    for (unsigned i = 0; i < note_count_; ++i) {
        if (notes_[i].key != key || notes_[i].grade != Grade::Pending) continue;
        const Time delta = abs_time(at - (repeat_start_ + notes_[i].at));
        if (delta < closest) { closest = delta; best = i; }
    }
    if (best == kMaxNotes) {
        // Bound counters under deliberately abusive input streams.
        if (stats_.stray < 100000) ++stats_.stray;
        stats_.combo = 0; last_combo_at_ = at; update_score(); return Grade::Stray;
    }
    const Time target = repeat_start_ + notes_[best].at;
    // A skipped older note breaks the combo; do not prematurely mark it Miss.
    for (unsigned i = 0; i < note_count_; ++i)
        if (notes_[i].grade == Grade::Pending && repeat_start_ + notes_[i].at + windows_.bad < at &&
            repeat_start_ + notes_[i].at + windows_.bad > last_combo_at_)
            stats_.combo = 0;
    const Grade grade = closest <= windows_.perfect ? Grade::Perfect :
                        closest <= windows_.good ? Grade::Good : Grade::Bad;
    notes_[best].grade = grade;
    if (grade == Grade::Perfect) ++stats_.perfect;
    else if (grade == Grade::Good) ++stats_.good;
    else ++stats_.bad;
    if (at > last_combo_at_) last_combo_at_ = at;
    ++stats_.combo;
    if (stats_.combo > stats_.max_combo) stats_.max_combo = stats_.combo;
    stats_.signed_error_us += at - target;
    stats_.absolute_error_us += closest;
    update_score(); return grade;
}
int Engine::beat_index(Time now) const {
    Time start = 0, length = 0;
    if (phase_ == Phase::CountIn) { start = count_start_; length = 4 * beat_us(); }
    else if (phase_ == Phase::Listen) { start = listen_start_; length = duration_; }
    else if (phase_ == Phase::Ready) { start = ready_start_; length = 4 * beat_us(); }
    else if (phase_ == Phase::Repeat) { start = repeat_start_; length = duration_; }
    else return -1;
    if (now < start || now >= start + length) return -1;
    return static_cast<int>(((now - start) / beat_us()) % 4);
}
uint8_t Engine::demonstration_mask(Time now) const {
    if (phase_ != Phase::Listen) return 0;
    uint8_t mask = 0;
    for (unsigned i = 0; i < note_count_; ++i) {
        const Time delta = now - listen_start_ - notes_[i].at;
        if (delta >= 0 && delta < 100000) mask |= static_cast<uint8_t>(1U << notes_[i].key);
    }
    return mask;
}
} // namespace beat_echo
