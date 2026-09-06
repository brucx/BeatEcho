#include "beat_echo/engine.hpp"
#include "beat_echo/synth.hpp"
using namespace beat_echo;
static Engine game;
static int16_t pcm[kSampleRate/2];
static Time micros(double ms) {
    // Bound conversion before float->integer; JS NaN/Infinity must not trap WASM.
    return (ms >= 0 && ms < 1e12) ? static_cast<Time>(ms*1000.0) : 0;
}
extern "C" {
void be_init(int bpm, int difficulty, int offset, unsigned seed) { game.reset({bpm,difficulty,offset},seed); }
int be_configure(int bpm, int difficulty, int offset) { return game.configure({bpm,difficulty,offset}); }
void be_select(double now_ms) { game.select(micros(now_ms)); }
void be_abort() { game.abort(); }
void be_tick(double now_ms) { game.tick(micros(now_ms)); }
int be_press(unsigned key, double now_ms) { return static_cast<int>(game.press(key,micros(now_ms))); }
int be_beat(double now_ms) { return game.beat_index(micros(now_ms)); }
unsigned be_demo_mask(double now_ms) { return game.demonstration_mask(micros(now_ms)); }
double be_value(unsigned field) {
    const auto& s=game.stats();
    switch(field) {
        case 0:return static_cast<int>(game.phase()); case 1:return game.settings().bpm;
        case 2:return game.settings().difficulty; case 3:return game.round(); case 4:return game.lives();
        case 5:return game.session_score(); case 6:return s.score; case 7:return s.accuracy;
        case 8:return s.combo; case 9:return s.max_combo; case 10:return game.note_count();
        case 11:return s.passed; case 12:return game.won(); case 13:return static_cast<double>(game.beat_us());
        case 14:return static_cast<double>(game.repeat_start()); case 15:return static_cast<double>(game.listen_start());
        case 16:return static_cast<double>(game.ready_start()); case 17:return static_cast<double>(game.count_start());
        case 18:return static_cast<double>(game.duration()); case 19:return static_cast<double>(game.end_at());
        case 20:return s.perfect; case 21:return s.good; case 22:return s.bad; case 23:return s.miss;
        case 24:return s.stray; case 25:return game.best_score(); case 26:return game.settings().input_offset_ms;
        case 27:return static_cast<double>(s.signed_error_us); case 28:return s.perfect+s.good+s.bad;
        case 29:return game.schedule().epoch; case 30:return game.schedule().count;
        default:return 0;
    }
}
double be_note(unsigned i,unsigned field) {
    if(i>=game.note_count()) return -1;
    auto n=game.notes()[i];
    return field==0?static_cast<double>(n.at):(field==1?n.key:static_cast<int>(n.grade));
}
double be_cue(unsigned i,unsigned field) {
    if(i>=game.schedule().count) return -1;
    auto c=game.schedule().cues[i];
    return field==0?static_cast<double>(c.at):(field==1?c.voice:c.velocity);
}
uintptr_t be_sound(unsigned instrument) {
    Synth synth; synth.reset(); synth.set_volume(100); synth.trigger(instrument);
    synth.render(pcm,kSampleRate/2); return reinterpret_cast<uintptr_t>(pcm);
}
unsigned be_sound_frames() { return kSampleRate/2; }
unsigned be_sample_rate() { return kSampleRate; }
}
