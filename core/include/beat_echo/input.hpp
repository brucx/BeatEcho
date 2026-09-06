#pragma once
#include "beat_echo/engine.hpp"
namespace beat_echo {
struct Edge { bool changed = false, pressed = false; Time at = 0; };
class Debouncer {
public:
    void reset(bool pressed, Time now) { stable_ = candidate_ = pressed; changed_at_ = now; }
    Edge sample(bool pressed, Time now) {
        if (pressed != candidate_) { candidate_ = pressed; changed_at_ = now; }
        const Time delay = candidate_ ? 5000 : 3000;
        if (stable_ == candidate_ || now - changed_at_ < delay) return {};
        stable_ = candidate_;
        return {true, stable_, changed_at_}; // Preserve capture time, not callback time.
    }
private:
    bool stable_ = false, candidate_ = false;
    Time changed_at_ = 0;
};
class Encoder {
public:
    void reset(uint8_t state) { state_ = state & 3U; quarter_ = 0; }
    int sample(uint8_t state) {
        state &= 3U;
        if ((state ^ state_) == 3U) { state_ = state; quarter_ = 0; return 0; }
        static constexpr int8_t movement[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
        quarter_ += movement[(state_ << 2U) | state]; state_ = state;
        if (quarter_ >= 4) { quarter_ -= 4; return 1; }
        if (quarter_ <= -4) { quarter_ += 4; return -1; }
        return 0;
    }
private:
    uint8_t state_ = 0;
    int quarter_ = 0;
};
} // namespace beat_echo
