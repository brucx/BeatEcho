#pragma once
#include <stddef.h>
#include <stdint.h>
namespace beat_echo {
constexpr uint32_t kSampleRate = 32000;
constexpr unsigned kVoices = 12;
constexpr unsigned kInstruments = 11; // 8 drums, click, success, failure.
class Synth {
public:
    void reset();
    void trigger(unsigned instrument, unsigned velocity = 100);
    int16_t next();
    void render(int16_t* output, size_t frames);
    void set_volume(unsigned value) { volume_ = value > 100 ? 100 : value; }
    unsigned active_voices() const;
private:
    struct Voice {
        uint32_t age = 0, length = 0, phase = 0, noise = 1;
        int32_t previous_noise = 0;
        uint8_t instrument = 0, velocity = 100;
        bool active = false;
    };
    Voice voices_[kVoices]{};
    unsigned volume_ = 35;
};
} // namespace beat_echo
