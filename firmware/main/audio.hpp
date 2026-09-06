#pragma once
#include <atomic>
#include "beat_echo/engine.hpp"
#include "beat_echo/synth.hpp"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
class Audio {
public:
    esp_err_t begin();
    bool publish(const beat_echo::Schedule& schedule);
    bool play(unsigned voice,unsigned velocity=100);
    void volume(unsigned level) { volume_.store(level>60?60:level); }
    beat_echo::Time at_raw_time(int64_t raw_us);
    beat_echo::Time now();
    unsigned faults() const { return faults_.load(); }
private:
    struct Strike { uint32_t epoch; uint8_t voice,velocity; };
    static constexpr unsigned kFrames=128, kDmaBuffers=3;
    static bool on_sent(i2s_chan_handle_t,i2s_event_data_t*,void*);
    static bool on_overflow(i2s_chan_handle_t,i2s_event_data_t*,void*);
    static void entry(void*);
    void run();
    void release_before_start();
    uint64_t played_frames();
    bool receive_schedule();
    i2s_chan_handle_t channel_=nullptr;
    QueueHandle_t schedules_=nullptr, strikes_=nullptr;
    SemaphoreHandle_t start_gate_=nullptr;
    TaskHandle_t task_=nullptr;
    portMUX_TYPE clock_lock_=portMUX_INITIALIZER_UNLOCKED;
    uint64_t played_=0;
    int64_t last_dma_raw_=0;
    uint64_t render_frame_=0;
    std::atomic<unsigned> faults_{0},volume_{25};
    std::atomic<uint32_t> publish_epoch_{0};
    beat_echo::Schedule schedule_{};
    unsigned next_cue_=0;
    beat_echo::Synth synth_;
    int16_t output_[kFrames*2]{};
};
