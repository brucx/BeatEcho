#include "beat_echo/engine.hpp"
#include "beat_echo/input.hpp"
#include "beat_echo/synth.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace beat_echo;
static unsigned checks = 0, cases = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
#define CASE(name) do { ++cases; std::printf("[%02u] %s\n", cases, name); } while (0)
static Engine make(Settings s = {}, uint32_t seed = 42) {
    Engine g; g.reset(s, seed); g.select(1000000); return g;
}
static void perfect(Engine& g, Time offset = 0) {
    for (unsigned i = 0; i < g.note_count(); ++i)
        CHECK(g.press(g.notes()[i].key, g.repeat_start() + g.notes()[i].at + offset) == Grade::Perfect);
    g.tick(g.end_at());
}
int main() {
    CASE("settings clamp, including invalid BPM and calibration"); {
        Engine g; g.reset({0, 20, 1000}, 0);
        CHECK(g.settings().bpm == 60); CHECK(g.settings().difficulty == 2);
        CHECK(g.settings().input_offset_ms == 200); CHECK(g.phase() == Phase::Menu);
        CHECK(g.configure({1000, -1, -1000})); CHECK(g.settings().bpm == 160);
        CHECK(g.settings().difficulty == 0); CHECK(g.settings().input_offset_ms == -200);
    }
    CASE("four-beat count-in, demonstration, ready, repeat and result"); {
        auto g = make(); CHECK(g.phase() == Phase::CountIn);
        g.tick(g.listen_start()); CHECK(g.phase() == Phase::Listen);
        g.tick(g.ready_start()); CHECK(g.phase() == Phase::Ready);
        g.tick(g.repeat_start()); CHECK(g.phase() == Phase::Repeat);
        g.tick(g.end_at()); CHECK(g.phase() == Phase::Result);
        CHECK(g.stats().miss == g.note_count()); CHECK(g.lives() == 2);
    }
    CASE("inclusive perfect/good/bad boundary windows"); {
        const Time offsets[] = {-150000,-100001,-100000,-50001,-50000,0,50000,50001,100000,100001,150000};
        const Grade want[] = {Grade::Bad,Grade::Bad,Grade::Good,Grade::Good,Grade::Perfect,Grade::Perfect,
                              Grade::Perfect,Grade::Good,Grade::Good,Grade::Bad,Grade::Bad};
        for (unsigned i = 0; i < sizeof(offsets)/sizeof(*offsets); ++i) {
            auto g = make(); CHECK(g.press(0, g.repeat_start()+offsets[i]) == want[i]);
        }
    }
    CASE("outside the first early window is ignored"); {
        auto g = make(); CHECK(g.press(0, g.repeat_start()-150001) == Grade::Ignored);
        CHECK(g.stats().stray == 0);
    }
    CASE("late input cannot claim an expired target"); {
        auto g = make(); CHECK(g.press(0, g.repeat_start()+150001) == Grade::Stray);
    }
    CASE("wrong keys, duplicate key-downs and score clamp"); {
        auto g = make(); CHECK(g.press(7, g.repeat_start()) == Grade::Stray);
        CHECK(g.stats().score == 0);
        CHECK(g.press(0, g.repeat_start()) == Grade::Perfect);
        CHECK(g.press(0, g.repeat_start()) == Grade::Stray);
        CHECK(g.stats().perfect == 1); CHECK(g.stats().score == 800);
    }
    CASE("out-of-range keys ignored safely"); {
        auto g = make(); CHECK(g.press(8, g.repeat_start()) == Grade::Ignored);
        CHECK(g.press(0xffffffffU, g.repeat_start()) == Grade::Ignored);
    }
    CASE("demo input is not scored"); {
        auto g = make(); CHECK(g.press(0, g.listen_start()) == Grade::Ignored);
        CHECK(g.stats().score == 0);
    }
    CASE("perfect phrase is 100 percent, awarded once"); {
        auto g = make(); const unsigned n = g.note_count(); perfect(g);
        CHECK(g.stats().accuracy == 1000); CHECK(g.stats().passed);
        CHECK(g.session_score() == static_cast<int>(n*1000)); CHECK(g.lives() == 3);
        g.tick(g.end_at()+1000000); CHECK(g.session_score() == static_cast<int>(n*1000));
    }
    CASE("passing advances round; next pattern gets a new epoch"); {
        auto g = make(); auto epoch = g.schedule().epoch; perfect(g);
        g.select(g.end_at()+500000); CHECK(g.round() == 2); CHECK(g.schedule().epoch > epoch);
    }
    CASE("failure retry preserves exact phrase; no score farming"); {
        auto g = make(); Note phrase[kMaxNotes]; auto n = g.note_count();
        for (unsigned i=0;i<n;++i) phrase[i] = g.notes()[i];
        g.tick(g.end_at()); CHECK(g.session_score() == 0);
        g.select(g.end_at()+1000000); CHECK(g.round() == 1); CHECK(g.note_count() == n);
        for (unsigned i=0;i<n;++i) { CHECK(phrase[i].at == g.notes()[i].at); CHECK(phrase[i].key == g.notes()[i].key); }
    }
    CASE("three failures end session"); {
        auto g = make();
        for (unsigned n=0;n<3;++n) { g.tick(g.end_at()); if(n<2) g.select(g.end_at()+1000000); }
        CHECK(g.phase() == Phase::GameOver); CHECK(g.lives() == 0); CHECK(!g.won());
        g.select(g.end_at()+1000000); CHECK(g.round()==1); CHECK(g.lives()==3);
    }
    CASE("12-round victory and RAM best score survive new session"); {
        auto g = make();
        for (unsigned n=1;n<=kFinalRound;++n) {
            CHECK(g.round()==n); perfect(g);
            if(n<kFinalRound) g.select(g.end_at()+1000000);
        }
        CHECK(g.phase()==Phase::GameOver); CHECK(g.won());
        auto high = g.best_score(); CHECK(high>0);
        g.select(g.end_at()+1000000); CHECK(g.session_score()==0); CHECK(g.best_score()==high);
    }
    CASE("captured timestamps win over delayed delivery"); {
        auto g = make(); auto t = g.repeat_start();
        g.tick(t+300000); CHECK(g.press(0,t)==Grade::Perfect);
    }
    CASE("calibration positive subtracts; negative adds"); {
        auto g = make({90,0,80}); CHECK(g.press(0,g.repeat_start()+80000)==Grade::Perfect);
        auto h = make({90,0,-80}); CHECK(h.press(0,h.repeat_start()-80000)==Grade::Perfect);
    }
    CASE("positive calibration extends final input delivery horizon"); {
        auto a=make({90,0,0}); auto b=make({90,0,200});
        CHECK(b.end_at()-a.end_at()==200000);
    }
    CASE("finalized rounds reject retroactive input"); {
        auto g=make(); auto t=g.repeat_start(); g.tick(g.end_at());
        CHECK(g.press(0,t)==Grade::Ignored);
    }
    CASE("abort cancels cue schedule and active judgement"); {
        auto g=make(); auto epoch=g.schedule().epoch; g.abort();
        CHECK(g.phase()==Phase::Menu); CHECK(g.schedule().count==0);
        CHECK(g.schedule().epoch>epoch); CHECK(g.press(0,g.repeat_start())==Grade::Ignored);
    }
    CASE("configuration frozen during a session"); {
        auto g=make(); CHECK(!g.configure({140,2,0})); CHECK(g.settings().bpm==90);
        g.abort(); CHECK(g.configure({140,2,0}));
    }
    CASE("tick cannot rewind phase or lose later state"); {
        auto g=make(); g.tick(g.repeat_start()); g.tick(g.listen_start()); CHECK(g.phase()==Phase::Repeat);
    }
    CASE("select during active gameplay is harmless"); {
        auto g=make(); auto at=g.repeat_start(); g.select(at); CHECK(g.repeat_start()==at);
    }
    CASE("beat lights and demo key hint are bounded"); {
        auto g=make(); CHECK(g.beat_index(g.count_start()-1)==-1);
        CHECK(g.beat_index(g.count_start())==0); g.tick(g.listen_start());
        CHECK(g.demonstration_mask(g.listen_start()) & 1);
        CHECK(g.demonstration_mask(g.listen_start()+100001)==0);
    }
    CASE("millisecond rollover is irrelevant to 64-bit timeline"); {
        auto g=make(); g.abort(); g.select((1LL<<32)*1000+2000000);
        perfect(g); CHECK(g.stats().passed);
    }
    CASE("seed determinism over 12 rounds"); {
        auto a=make({137,2,0},777), b=make({137,2,0},777);
        for(unsigned n=1;n<=12;++n) {
            CHECK(a.note_count()==b.note_count());
            for(unsigned i=0;i<a.note_count();++i) {
                CHECK(a.notes()[i].key==b.notes()[i].key); CHECK(a.notes()[i].at==b.notes()[i].at);
            }
            perfect(a); perfect(b); a.select(a.end_at()+1000000); b.select(b.end_at()+1000000);
        }
    }
    CASE("patterns, chords and cue capacities: 14400 generated rounds"); {
        unsigned chords=0, two_bars=0;
        for(unsigned seed=1;seed<=100;++seed) for(int d=0;d<3;++d) for(int bpm : {60,90,137,160}) {
            auto g=make({bpm,d,0},seed);
            for(unsigned round=1;round<=12;++round) {
                CHECK(g.note_count()>0 && g.note_count()<=kMaxNotes);
                CHECK(g.schedule().count<=kMaxCues);
                CHECK(g.schedule().count==8+g.bars()*8+g.note_count());
                if(g.bars()==2) ++two_bars;
                for(unsigned i=0;i<g.note_count();++i) {
                    CHECK(g.notes()[i].key<8); CHECK(g.notes()[i].at>=0 && g.notes()[i].at<g.duration());
                    if(i) {
                        CHECK(g.notes()[i].at>=g.notes()[i-1].at);
                        if(g.notes()[i].at==g.notes()[i-1].at) {
                            CHECK(g.notes()[i].key!=g.notes()[i-1].key); ++chords;
                        }
                    }
                }
                for(unsigned i=1;i<g.schedule().count;++i) CHECK(g.schedule().cues[i].at>=g.schedule().cues[i-1].at);
                perfect(g); CHECK(g.stats().accuracy==1000);
                if(round<12) g.select(g.end_at()+1000000);
            }
        }
        CHECK(chords>0); CHECK(two_bars>0);
    }
    CASE("independent chord notes accept reversed delivery order"); {
        auto g=make({120,2,0},4); perfect(g); g.select(g.end_at()+1000000); perfect(g); g.select(g.end_at()+1000000);
        for(unsigned i=g.note_count();i>0;--i)
            CHECK(g.press(g.notes()[i-1].key,g.repeat_start()+g.notes()[i-1].at)==Grade::Perfect);
        g.tick(g.end_at()); CHECK(g.stats().accuracy==1000);
    }
    CASE("missed note breaks combo only once, not every later hit"); {
        auto g=make({90,1,0},1);
        for(uint32_t seed=2;g.note_count()<3 && seed<100;++seed) g=make({90,1,0},seed);
        CHECK(g.note_count()>=3);
        for(unsigned i=1;i<g.note_count();++i) g.press(g.notes()[i].key,g.repeat_start()+g.notes()[i].at);
        CHECK(g.stats().combo==g.note_count()-1);
    }
    CASE("debounce captures stable-candidate timestamp after 5ms"); {
        Debouncer d; d.reset(false,0);
        CHECK(!d.sample(true,1000).changed); CHECK(!d.sample(true,5999).changed);
        auto e=d.sample(true,6000); CHECK(e.changed && e.pressed); CHECK(e.at==1000);
    }
    CASE("switch bounce rejected and hold does not autorepeat"); {
        Debouncer d; d.reset(false,0); d.sample(true,1000); d.sample(false,2000);
        d.sample(true,3000); CHECK(!d.sample(true,7000).changed);
        CHECK(d.sample(true,8000).changed); CHECK(!d.sample(true,900000).changed);
    }
    CASE("release uses 3ms, allowing rapid repeated presses"); {
        Debouncer d; d.reset(true,0); d.sample(false,1000);
        CHECK(!d.sample(false,3999).changed); auto e=d.sample(false,4000);
        CHECK(e.changed && !e.pressed); d.sample(true,5000); CHECK(d.sample(true,10000).changed);
    }
    CASE("held at startup does not create a synthetic strike"); {
        Debouncer d; d.reset(true,0); CHECK(!d.sample(true,100000).changed);
    }
    CASE("encoder complete detent and reverse, no false half step"); {
        Encoder e; e.reset(0); CHECK(e.sample(2)==0); CHECK(e.sample(3)==0);
        CHECK(e.sample(1)==0); CHECK(e.sample(0)==1);
        CHECK(e.sample(1)==0); CHECK(e.sample(3)==0); CHECK(e.sample(2)==0); CHECK(e.sample(0)==-1);
    }
    CASE("illegal encoder transition resets accumulated motion"); {
        Encoder e; e.reset(0); e.sample(2); CHECK(e.sample(1)==0); CHECK(e.sample(1)==0);
    }
    CASE("all 11 original synthesized voices emit and decay to zero"); {
        Synth s; s.set_volume(100); std::vector<int16_t> pcm(kSampleRate);
        for(unsigned voice=0;voice<kInstruments;++voice) {
            s.reset(); s.trigger(voice); s.render(pcm.data(),pcm.size());
            int64_t energy=0; for(auto sample:pcm) energy+=static_cast<int64_t>(sample)*sample;
            CHECK(energy>100000); CHECK(s.active_voices()==0); CHECK(pcm.back()==0);
        }
    }
    CASE("synth deterministic across resets"); {
        Synth s; int16_t a[1000],b[1000]; s.reset(); s.trigger(7); s.render(a,1000);
        s.reset(); s.trigger(7); s.render(b,1000); CHECK(std::memcmp(a,b,sizeof(a))==0);
    }
    CASE("synth voice stealing, zero volume, invalid instrument"); {
        Synth s; s.reset(); for(unsigned i=0;i<100;++i) s.trigger(i%8);
        CHECK(s.active_voices()==kVoices); s.set_volume(0);
        for(unsigned i=0;i<1000;++i) CHECK(s.next()==0);
        s.reset(); s.trigger(99); CHECK(s.active_voices()==0);
    }
    CASE("eight-note chord is mixed without undefined overflow"); {
        Synth s; s.set_volume(100); for(unsigned i=0;i<8;++i) s.trigger(i);
        int64_t energy=0; for(unsigned i=0;i<20000;++i) { auto x=s.next(); energy+=static_cast<int64_t>(x)*x; }
        CHECK(energy>0); CHECK(s.active_voices()==0);
    }
    std::printf("PASS: %u cases, %u checks\n", cases, checks);
}
