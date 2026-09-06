#include <initializer_list>
#include "board.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
namespace board {
namespace {
esp_err_t low_output(gpio_num_t pin) {
    esp_err_t e=gpio_set_level(pin,0); if(e!=ESP_OK) return e;
    return gpio_set_direction(pin,GPIO_MODE_OUTPUT);
}
esp_err_t safe_commands() {
    for(auto p : {GPIO_NUM_9,GPIO_NUM_10,GPIO_NUM_12,GPIO_NUM_13,GPIO_NUM_14,GPIO_NUM_15}) {
        esp_err_t e=low_output(p); if(e!=ESP_OK) return e;
    }
    gpio_config_t input{};
    input.pin_bit_mask=1ULL<<11; input.mode=GPIO_MODE_DISABLE;
    input.pull_up_en=GPIO_PULLUP_DISABLE; input.pull_down_en=GPIO_PULLDOWN_DISABLE;
    input.intr_type=GPIO_INTR_DISABLE;
    return gpio_config(&input); // Mic DATA is floating, never actively driven low.
}
}
esp_err_t Hardware::begin() {
    esp_err_t e=low_output(power); if(e!=ESP_OK) return e;
    e=safe_commands(); if(e!=ESP_OK) return e;
    e=low_output(status); if(e!=ESP_OK) return e;
    e=low_output(GPIO_NUM_5); if(e!=ESP_OK) return e; // Battery divider disabled.
    gpio_config_t in{};
    for(auto key:keys) in.pin_bit_mask|=1ULL<<key;
    in.pin_bit_mask|=(1ULL<<encoder_a)|(1ULL<<encoder_b)|(1ULL<<encoder_press);
    in.mode=GPIO_MODE_INPUT; in.pull_up_en=GPIO_PULLUP_ENABLE;
    in.pull_down_en=GPIO_PULLDOWN_DISABLE; in.intr_type=GPIO_INTR_DISABLE;
    e=gpio_config(&in); if(e!=ESP_OK) return e;
    e=gpio_set_level(power,1); if(e!=ESP_OK) return e;
    // This is an explicitly unqualified project delay, not a board fact.
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BE_POWER_SETTLE_MS));
    rmt_tx_channel_config_t tx{};
    tx.gpio_num=led; tx.clk_src=RMT_CLK_SRC_DEFAULT; tx.resolution_hz=10000000;
    tx.mem_block_symbols=64; tx.trans_queue_depth=1;
    e=rmt_new_tx_channel(&tx,&tx_); if(e!=ESP_OK) return e;
    rmt_copy_encoder_config_t copy{};
    e=rmt_new_copy_encoder(&copy,&copy_); if(e!=ESP_OK) return e;
    e=rmt_enable(tx_); if(e!=ESP_OK) return e;
    enabled_=true;
    Pixel black[5]{}; return show(black);
}
esp_err_t Hardware::show(const Pixel (&pixels)[5]) {
    if(!enabled_ || failed_) return ESP_ERR_INVALID_STATE;
    unsigned n=0;
    for(const auto& p:pixels) {
        // GRB order. Limit physical brightness independently from UI colors.
        const uint8_t bytes[3]={p.g,p.r,p.b};
        for(uint8_t byte:bytes) for(int bit=7;bit>=0;--bit) {
            const bool one=(byte & (1U<<bit))!=0;
            auto& symbol=symbols_[n++]; symbol.val=0;
            symbol.level0=1; symbol.duration0=one?8:4;
            symbol.level1=0; symbol.duration1=one?5:9;
        }
    }
    symbols_[n].val=0; symbols_[n].duration0=1500; symbols_[n].duration1=1500; // 300us reset low.
    rmt_transmit_config_t config{};
    esp_err_t e=rmt_transmit(tx_,copy_,symbols_,sizeof(symbols_),&config);
    if(e==ESP_OK) e=rmt_tx_wait_all_done(tx_,20);
    if(e!=ESP_OK) {
        failed_=true; // Keep the persistent payload immutable after a timeout.
        if(rmt_disable(tx_)==ESP_OK) enabled_=false;
    }
    return e;
}
void Hardware::power_off_after_quiesce() {
    if(tx_ && enabled_) { (void)rmt_disable(tx_); enabled_=false; }
    if(copy_) { (void)rmt_del_encoder(copy_); copy_=nullptr; }
    if(tx_) { (void)rmt_del_channel(tx_); tx_=nullptr; }
    (void)safe_commands(); (void)gpio_set_level(power,0);
}
} // namespace board
