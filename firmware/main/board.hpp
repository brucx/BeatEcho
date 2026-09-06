#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
namespace board {
// Hardware facts only. Source and immutable baseline hash: docs/HARDWARE.md.
constexpr gpio_num_t keys[8] = {GPIO_NUM_2,GPIO_NUM_47,GPIO_NUM_38,GPIO_NUM_41,
                              GPIO_NUM_1,GPIO_NUM_6,GPIO_NUM_7,GPIO_NUM_48};
constexpr gpio_num_t encoder_a=GPIO_NUM_17, encoder_b=GPIO_NUM_16, encoder_press=GPIO_NUM_18;
constexpr gpio_num_t power=GPIO_NUM_8, led=GPIO_NUM_12, status=GPIO_NUM_42;
constexpr gpio_num_t spk_bclk=GPIO_NUM_14, spk_ws=GPIO_NUM_13, spk_data=GPIO_NUM_15;
struct Pixel { uint8_t r=0,g=0,b=0; };
class Hardware {
public:
    esp_err_t begin();
    esp_err_t show(const Pixel (&pixels)[5]);
    // Call only after every I2S/LED user is stopped. Never use this to blink LEDs.
    void power_off_after_quiesce();
    bool pressed(unsigned key) const { return key<8 && gpio_get_level(keys[key])==0; }
    uint8_t encoder_state() const { return static_cast<uint8_t>((gpio_get_level(encoder_a)<<1)|gpio_get_level(encoder_b)); }
private:
    rmt_channel_handle_t tx_=nullptr;
    rmt_encoder_handle_t copy_=nullptr;
    rmt_symbol_word_t symbols_[121]{};
    bool enabled_=false, failed_=false;
};
} // namespace board
