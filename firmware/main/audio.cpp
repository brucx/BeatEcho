#include "audio.hpp"
#include "board.hpp"
#include "esp_attr.h"
#include "esp_timer.h"
#include "sdkconfig.h"
using namespace beat_echo;
static_assert(std::atomic<unsigned>::is_always_lock_free,"ISR counters must be lock-free");
bool IRAM_ATTR Audio::on_sent(i2s_chan_handle_t,i2s_event_data_t* event,void* context) {
    auto* self=static_cast<Audio*>(context);
    portENTER_CRITICAL_ISR(&self->clock_lock_);
    self->played_+=event->size/(2*sizeof(int16_t));
    self->last_dma_raw_=esp_timer_get_time();
    portEXIT_CRITICAL_ISR(&self->clock_lock_);
    return false;
}
bool IRAM_ATTR Audio::on_overflow(i2s_chan_handle_t,i2s_event_data_t*,void* context) {
    static_cast<Audio*>(context)->faults_.fetch_add(1); return false;
}
Time Audio::at_raw_time(int64_t raw_us) {
    portENTER_CRITICAL(&clock_lock_);
    const uint64_t frames=played_; const int64_t anchor=last_dma_raw_;
    portEXIT_CRITICAL(&clock_lock_);
    // DMA presentation frames anchor the clock. Extrapolate only the partial block.
    // Driver/DAC/acoustic offset still requires a real hardware calibration.
    return static_cast<Time>(frames*1000000/kSampleRate)+(raw_us-anchor);
}
Time Audio::now() { return at_raw_time(esp_timer_get_time()); }
uint64_t Audio::played_frames() {
    portENTER_CRITICAL(&clock_lock_); const uint64_t value=played_; portEXIT_CRITICAL(&clock_lock_);
    return value;
}
void Audio::release_before_start() {
    if(task_) { vTaskDelete(task_); task_=nullptr; }
    if(channel_) { (void)i2s_del_channel(channel_); channel_=nullptr; }
    if(schedules_) { vQueueDelete(schedules_); schedules_=nullptr; }
    if(strikes_) { vQueueDelete(strikes_); strikes_=nullptr; }
    if(start_gate_) { vSemaphoreDelete(start_gate_); start_gate_=nullptr; }
}
esp_err_t Audio::begin() {
    volume(CONFIG_BE_VOLUME);
    schedules_=xQueueCreate(1,sizeof(Schedule)); strikes_=xQueueCreate(64,sizeof(Strike));
    start_gate_=xSemaphoreCreateBinary();
    if(!schedules_ || !strikes_ || !start_gate_) { release_before_start(); return ESP_ERR_NO_MEM; }
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    channel.dma_desc_num=kDmaBuffers; channel.dma_frame_num=kFrames;
    channel.auto_clear_after_cb=true;
    esp_err_t err=i2s_new_channel(&channel,&channel_,nullptr);
    if(err!=ESP_OK) { release_before_start(); return err; }
    i2s_std_config_t config{};
    config.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
    config.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk=I2S_GPIO_UNUSED; config.gpio_cfg.bclk=board::spk_bclk;
    config.gpio_cfg.ws=board::spk_ws; config.gpio_cfg.dout=board::spk_data;
    config.gpio_cfg.din=I2S_GPIO_UNUSED;
    err=i2s_channel_init_std_mode(channel_,&config);
    if(err!=ESP_OK) { release_before_start(); return err; }
    i2s_event_callbacks_t callbacks{}; callbacks.on_sent=on_sent; callbacks.on_send_q_ovf=on_overflow;
    err=i2s_channel_register_event_callback(channel_,&callbacks,this);
    if(err!=ESP_OK) { release_before_start(); return err; }
    int16_t silence[kFrames*kDmaBuffers*2]{}; size_t loaded=0;
    err=i2s_channel_preload_data(channel_,silence,sizeof(silence),&loaded);
    if(err!=ESP_OK || loaded!=sizeof(silence)) { release_before_start(); return err==ESP_OK?ESP_FAIL:err; }
    render_frame_=loaded/(2*sizeof(int16_t));
    if(xTaskCreatePinnedToCore(entry,"be_audio",6144,this,12,&task_,1)!=pdPASS) {
        release_before_start(); return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&clock_lock_); played_=0; last_dma_raw_=esp_timer_get_time(); portEXIT_CRITICAL(&clock_lock_);
    err=i2s_channel_enable(channel_);
    if(err!=ESP_OK) { release_before_start(); return err; }
    xSemaphoreGive(start_gate_);
    return ESP_OK;
}
bool Audio::publish(const Schedule& schedule) {
    if(!schedules_ || faults()) return false;
    if(xQueueOverwrite(schedules_,&schedule)!=pdPASS) { faults_.fetch_add(1); return false; }
    publish_epoch_.store(schedule.epoch); return true;
}
bool Audio::play(unsigned voice,unsigned velocity) {
    if(!strikes_ || faults() || voice>=kInstruments) return false;
    Strike strike{publish_epoch_.load(),static_cast<uint8_t>(voice),static_cast<uint8_t>(velocity>100?100:velocity)};
    if(xQueueSend(strikes_,&strike,0)!=pdPASS) { faults_.fetch_add(1); return false; }
    return true;
}
bool Audio::receive_schedule() {
    if(xQueueReceive(schedules_,&schedule_,0)!=pdPASS) return false;
    next_cue_=0; synth_.reset(); return true;
}
void Audio::entry(void* context) { static_cast<Audio*>(context)->run(); }
void Audio::run() {
    xSemaphoreTake(start_gate_,portMAX_DELAY);
    for(;;) {
        receive_schedule();
        Strike strike{};
        while(xQueueReceive(strikes_,&strike,0)==pdPASS) {
            if(strike.epoch!=schedule_.epoch) receive_schedule();
            if(strike.epoch==schedule_.epoch && !faults()) synth_.trigger(strike.voice,strike.velocity);
        }
        if(played_frames()>render_frame_) faults_.fetch_add(1);
        synth_.set_volume(volume_.load());
        if(faults()) { synth_.reset(); schedule_.count=0; } // Latched: reboot to trust timing again.
        for(unsigned i=0;i<kFrames;++i) {
            const Time sample_time=static_cast<Time>((render_frame_+i)*1000000/kSampleRate);
            while(next_cue_<schedule_.count && schedule_.cues[next_cue_].at<=sample_time) {
                const Cue& cue=schedule_.cues[next_cue_++];
                if(sample_time-cue.at>30000) { faults_.fetch_add(1); synth_.reset(); break; }
                synth_.trigger(cue.voice,cue.velocity);
            }
            const int16_t mono=faults()?0:synth_.next();
            output_[2*i]=mono; output_[2*i+1]=mono; // Safe for either MAX98357 channel selection.
        }
        size_t written=0;
        const esp_err_t err=i2s_channel_write(channel_,output_,sizeof(output_),&written,100);
        if(err!=ESP_OK || written!=sizeof(output_)) faults_.fetch_add(1);
        render_frame_+=written/(2*sizeof(int16_t));
        if(err!=ESP_OK) vTaskDelay(pdMS_TO_TICKS(1));
    }
}
