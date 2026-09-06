#pragma once
#include <stddef.h>
#include <stdint.h>

namespace beat_echo {
using Time = int64_t;  // Microseconds on ONE audio clock, not wall/calendar time.
constexpr unsigned kKeys = 8;
constexpr unsigned kMaxNotes = 64;
constexpr unsigned kMaxCues = 128;
constexpr unsigned kFinalRound = 12;
constexpr Time kJudgementGraceUs = 30000; // Delivery grace; NOT a larger hit window.
enum class Phase : uint8_t { Menu, CountIn, Listen, Ready, Repeat, Result, GameOver };
enum class Grade : uint8_t { Pending, Perfect, Good, Bad, Miss, Stray, Ignored };
struct Settings {
    int bpm = 90;
    int difficulty = 0; // 0=easy, 1=normal, 2=hard
    int input_offset_ms = 0; // Positive subtracts from input time; range +/-200 ms.
};
struct Note { Time at = 0; uint8_t key = 0; Grade grade = Grade::Pending; };
struct Cue { Time at = 0; uint8_t voice = 0; uint8_t velocity = 100; };
struct Schedule { uint32_t epoch = 0; unsigned count = 0; Cue cues[kMaxCues]{}; };
struct Windows { Time perfect = 50000, good = 100000, bad = 150000; };
struct RoundStats {
    unsigned perfect = 0, good = 0, bad = 0, miss = 0, stray = 0;
    unsigned combo = 0, max_combo = 0;
    int score = 0, accuracy = 0; // Accuracy is per mille; stray presses are penalized.
    int64_t signed_error_us = 0, absolute_error_us = 0;
    bool passed = false;
};
class Engine {
public:
    void reset(Settings settings = {}, uint32_t seed = 1);
    bool configure(Settings settings); // Refuses changes during a session.
    void select(Time now);            // Start / next round / retry / new session.
    void abort();                     // Invalidates scheduled playback immediately.
    void tick(Time now);
    Grade press(unsigned key, Time captured_at);
    Phase phase() const { return phase_; }
    const Settings& settings() const { return settings_; }
    const Windows& windows() const { return windows_; }
    const RoundStats& stats() const { return stats_; }
    const Schedule& schedule() const { return schedule_; }
    const Note* notes() const { return notes_; }
    unsigned note_count() const { return note_count_; }
    unsigned round() const { return round_; }
    unsigned lives() const { return lives_; }
    int session_score() const { return session_score_; }
    int best_score() const { return best_score_; }
    bool won() const { return won_; }
    Time beat_us() const { return 60000000LL / settings_.bpm; }
    Time count_start() const { return count_start_; }
    Time listen_start() const { return listen_start_; }
    Time ready_start() const { return ready_start_; }
    Time repeat_start() const { return repeat_start_; }
    Time end_at() const { return end_at_; }
    Time duration() const { return duration_; }
    unsigned bars() const { return bars_; }
    int beat_index(Time now) const;
    uint8_t demonstration_mask(Time now) const;
    bool active() const;
private:
    void begin_round(Time now);
    void generate_pattern();
    void add_note(Time at, unsigned key);
    void add_cue(Time at, unsigned voice, unsigned velocity);
    void update_score();
    void finish();
    Settings settings_{};
    Windows windows_{};
    Phase phase_ = Phase::Menu;
    uint32_t seed_ = 1;
    Schedule schedule_{};
    Note notes_[kMaxNotes]{};
    unsigned note_count_ = 0, round_ = 1, lives_ = 3, bars_ = 1;
    RoundStats stats_{};
    int session_score_ = 0, best_score_ = 0;
    bool won_ = false;
    Time count_start_ = 0, listen_start_ = 0, ready_start_ = 0;
    Time repeat_start_ = 0, end_at_ = 0, duration_ = 0, last_tick_ = 0, last_combo_at_ = 0;
};
uint32_t random_step(uint32_t& state);
} // namespace beat_echo
