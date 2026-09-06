#include "beat_echo/synth.hpp"
namespace beat_echo {
namespace {
constexpr int16_t sine[256] = {
#include "sine_table.inc"
};
constexpr uint16_t durations_ms[kInstruments] = {320,200,70,180,240,200,350,450,35,220,220};
int32_t noise(uint32_t& n) {
    n ^= n << 13; n ^= n >> 17; n ^= n << 5;
    return static_cast<int32_t>(n >> 16) - 32768;
}
}
void Synth::reset() { for (auto& v : voices_) v = {}; }
void Synth::trigger(unsigned instrument, unsigned velocity) {
    if (instrument >= kInstruments || velocity == 0) return;
    unsigned slot = 0; uint32_t oldest = 0;
    for (unsigned i = 0; i < kVoices; ++i) {
        if (!voices_[i].active) { slot = i; break; }
        if (voices_[i].age >= oldest) { slot = i; oldest = voices_[i].age; }
    }
    Voice v{};
    v.active = true; v.instrument = static_cast<uint8_t>(instrument);
    v.velocity = static_cast<uint8_t>(velocity > 100 ? 100 : velocity);
    v.length = durations_ms[instrument] * (kSampleRate / 1000);
    v.noise = 0x12345678U ^ ((instrument + 1) * 0x9e3779b9U);
    voices_[slot] = v;
}
unsigned Synth::active_voices() const {
    unsigned n = 0; for (const auto& v : voices_) if (v.active) ++n;
    return n;
}
int16_t Synth::next() {
    int32_t mix = 0;
    for (auto& v : voices_) {
        if (!v.active) continue;
        const uint32_t ms = v.age / (kSampleRate / 1000);
        uint32_t hz = 180;
        switch (v.instrument) {
            case 0: hz = ms < 60 ? 55 + (60 - ms) * 2 : 55; break;
            case 4: hz = 110; break;
            case 5: hz = 175; break;
            case 8: hz = 1500; break;
            case 9: hz = ms < 110 ? 784 : 1047; break;
            case 10: hz = ms < 110 ? 220 : 147; break;
            default: break;
        }
        v.phase += static_cast<uint32_t>((static_cast<uint64_t>(hz) << 32U) / kSampleRate);
        int32_t wave = sine[v.phase >> 24U];
        const int32_t n = noise(v.noise);
        const int32_t high = (n - v.previous_noise) / 2;
        v.previous_noise = n;
        switch (v.instrument) {
            case 1: wave = (n * 3 + wave) / 4; break;
            case 2: case 6: wave = high; break;
            case 3:
                wave = n;
                if (ms < 45 && (ms % 15) >= 7) wave /= 6;
                break;
            case 7: wave = (high * 2 + ((v.phase & 0x10000000U) ? 10000 : -10000)) / 3; break;
            default: break;
        }
        int32_t envelope = static_cast<int32_t>((v.length - v.age) * 32767U / v.length);
        envelope = envelope * envelope / 32767;
        if (v.age < 32) envelope = envelope * static_cast<int32_t>(v.age) / 32;
        mix += (wave * envelope / 32767) * v.velocity / 100;
        if (++v.age >= v.length) v.active = false;
    }
    // Fixed headroom plus saturating output; never wrap PCM on a chord.
    mix = mix * static_cast<int32_t>(volume_) / 200;
    if (mix > 32767) mix = 32767;
    if (mix < -32768) mix = -32768;
    return static_cast<int16_t>(mix);
}
void Synth::render(int16_t* output, size_t frames) {
    if (!output) return;
    for (size_t i = 0; i < frames; ++i) output[i] = next();
}
} // namespace beat_echo
