#include <atomic>
#include <cinttypes>
#include "audio.hpp"
#include "board.hpp"
#include "beat_echo/engine.hpp"
#include "beat_echo/input.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sdkconfig.h"
using namespace beat_echo;
namespace {
constexpr const char* TAG="beat_echo";
board::Hardware hardware;
Audio audio;
Engine game;
QueueHandle_t inputs=nullptr;
std::atomic<unsigned> input_faults{0};
std::atomic<bool> led_fault{false};
enum class Kind:uint8_t { Strike, Turn, ButtonDown, ButtonUp };
struct Input { Kind kind; unsigned key; int step; Time captured_raw; };
void send(Input event) { if(xQueueSend(inputs,&event,0)!=pdPASS) input_faults.fetch_add(1); }
void scan_inputs(void*) {
    Debouncer keys[8],button;
    Encoder encoder;
    Time raw=esp_timer_get_time();
    for(unsigned i=0;i<8;++i) keys[i].reset(hardware.pressed(i),raw);
    button.reset(gpio_get_level(board::encoder_press)==0,raw);
    encoder.reset(hardware.encoder_state());
    TickType_t wake=xTaskGetTickCount();
    Time previous=raw;
    for(;;) {
        raw=esp_timer_get_time();
        if(raw-previous>20000) input_faults.fetch_add(1); // Stalled scan: invalidate, do not guess.
        previous=raw;
        for(unsigned i=0;i<8;++i) {
            Edge edge=keys[i].sample(hardware.pressed(i),raw);
            if(edge.changed && edge.pressed) send({Kind::Strike,i,0,edge.at});
        }
        int turn=encoder.sample(hardware.encoder_state());
#ifdef CONFIG_BE_ENCODER_REVERSE
        turn=-turn;
#endif
        if(turn) send({Kind::Turn,0,turn,raw});
        Edge edge=button.sample(gpio_get_level(board::encoder_press)==0,raw);
        if(edge.changed) send({edge.pressed?Kind::ButtonDown:Kind::ButtonUp,0,0,edge.at});
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(1));
    }
}
board::Pixel color(uint8_t r,uint8_t g,uint8_t b) { return {r,g,b}; }
board::Pixel phase_color(Phase phase) {
    switch(phase) {
        case Phase::Listen:return color(30,5,40);
        case Phase::Ready:return color(35,22,0);
        case Phase::Repeat:return color(0,35,25);
        default:return color(4,15,35);
    }
}
void paint(Time now,Grade feedback,Time feedback_until,bool fault) {
    board::Pixel pixels[5]{};
    if(fault) {
        if((esp_timer_get_time()/300000)%2) for(auto& pixel:pixels) pixel=color(36,0,0);
    } else if(game.phase()==Phase::Menu || game.phase()==Phase::GameOver) {
        if(game.phase()==Phase::GameOver) {
            for(auto& pixel:pixels) pixel=game.won()?color(0,28,12):color(30,0,3);
        } else {
            for(int i=0;i<=game.settings().difficulty;++i) pixels[i]=color(6,8,28);
            pixels[4]=color(2,10,8);
        }
    } else if(game.phase()==Phase::Result) {
        unsigned filled=static_cast<unsigned>(game.stats().accuracy)*4/1000;
        for(unsigned i=0;i<filled;++i) pixels[i]=game.stats().passed?color(0,28,18):color(30,12,0);
        pixels[4]=game.stats().passed?color(0,35,8):color(35,0,0);
    } else {
        const int beat=game.beat_index(now);
        for(unsigned i=0;i<4;++i) pixels[i]=color(1,1,2);
        if(beat>=0) pixels[beat]=phase_color(game.phase());
        pixels[4]=game.lives()==3?color(0,8,2):(game.lives()==2?color(12,6,0):color(14,0,0));
    }
    if(!fault && now<feedback_until) {
        pixels[4]=feedback==Grade::Perfect?color(0,40,12):
                  feedback==Grade::Good?color(30,24,0):
                  feedback==Grade::Bad?color(36,9,0):color(40,0,0);
    }
    const esp_err_t err=hardware.show(pixels);
    if(err!=ESP_OK) led_fault.store(true);
}
}
extern "C" void app_main() {
    ESP_LOGI(TAG,"Beat Echo 0.1.0 | EasyInput V2.0 | standalone, no HID/Wi-Fi/microphone");
    ESP_LOGW(TAG,"Peripheral settle=%d ms is PROJECT POLICY, not a measured board minimum",CONFIG_BE_POWER_SETTLE_MS);
    esp_err_t err=hardware.begin();
    if(err!=ESP_OK) {
        ESP_LOGE(TAG,"Hardware init: %s; leaving shared rail off",esp_err_to_name(err));
        hardware.power_off_after_quiesce(); return;
    }
    inputs=xQueueCreate(128,sizeof(Input));
    if(!inputs) { hardware.power_off_after_quiesce(); ESP_LOGE(TAG,"Input queue allocation failed"); return; }
    err=audio.begin();
    if(err!=ESP_OK) {
        ESP_LOGE(TAG,"Audio init: %s",esp_err_to_name(err));
        hardware.power_off_after_quiesce(); vQueueDelete(inputs); return;
    }
    game.reset({CONFIG_BE_DEFAULT_BPM,CONFIG_BE_DEFAULT_DIFFICULTY,CONFIG_BE_INPUT_OFFSET_MS},esp_random());
    audio.publish(game.schedule());
    if(xTaskCreatePinnedToCore(scan_inputs,"be_input",4096,nullptr,10,nullptr,0)!=pdPASS) {
        ESP_LOGE(TAG,"Input task allocation failed; no game starts. Restart required.");
        for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    unsigned volume=CONFIG_BE_VOLUME, seen_input_faults=0;
    uint32_t published=game.schedule().epoch;
    Time down_at=0, next_led=0, feedback_until=0;
    bool down=false,locked=false;
    Grade feedback=Grade::Ignored;
    Phase previous_phase=Phase::Menu;
    ESP_LOGI(TAG,"Rotate: BPM in menu, volume in game; click: start/next or abort; hold 800ms in menu: difficulty");
    for(;;) {
        const Time raw=esp_timer_get_time();
        const unsigned faults=input_faults.load();
        if((audio.faults() || led_fault.load()) && !locked) {
            locked=true; game.abort();
            ESP_LOGE(TAG,"Audio/LED fault: session INVALID. Power-cycle and inspect logs before retrying.");
        }
        if(faults!=seen_input_faults) {
            seen_input_faults=faults; game.abort(); down=false;
            Input discard{}; while(xQueueReceive(inputs,&discard,0)==pdPASS) {}
            ESP_LOGW(TAG,"Input/LED fault: round invalidated, input_faults=%u",faults);
        }
        Input event{};
        // Drain captured events BEFORE advancing the judgement deadline.
        while(xQueueReceive(inputs,&event,0)==pdPASS) {
            if(locked) continue;
            if(raw-event.captured_raw>100000) {
                game.abort(); down=false; ESP_LOGW(TAG,"Input delivery >100ms: session invalidated"); continue;
            }
            const Time at=audio.at_raw_time(event.captured_raw);
            if(event.kind==Kind::Strike) {
                Grade grade=game.press(event.key,at);
                if(!game.active() || grade!=Grade::Ignored) audio.play(event.key);
                if(grade!=Grade::Ignored) { feedback=grade; feedback_until=audio.now()+160000; }
            } else if(event.kind==Kind::Turn) {
                if(game.phase()==Phase::Menu || game.phase()==Phase::GameOver) {
                    Settings settings=game.settings(); settings.bpm+=event.step*5; game.configure(settings);
                    ESP_LOGI(TAG,"BPM=%d difficulty=%d",game.settings().bpm,game.settings().difficulty);
                } else {
                    int next=static_cast<int>(volume)+event.step*2;
                    volume=static_cast<unsigned>(next<0?0:(next>60?60:next)); audio.volume(volume);
                }
            } else if(event.kind==Kind::ButtonDown) {
                down=true; down_at=event.captured_raw;
            } else if(event.kind==Kind::ButtonUp && down) {
                down=false;
                const bool long_press=event.captured_raw-down_at>=800000;
                if(long_press && (game.phase()==Phase::Menu || game.phase()==Phase::GameOver)) {
                    Settings settings=game.settings(); settings.difficulty=(settings.difficulty+1)%3; game.configure(settings);
                    ESP_LOGI(TAG,"Difficulty=%d BPM=%d",game.settings().difficulty,game.settings().bpm);
                } else if(game.active()) game.abort();
                else game.select(audio.now());
            }
        }
        const Time now=audio.now();
        if(!locked) game.tick(now);
        if(game.schedule().epoch!=published) {
            published=game.schedule().epoch; audio.publish(game.schedule());
        }
        if(game.phase()!=previous_phase) {
            previous_phase=game.phase();
            ESP_LOGI(TAG,"Phase=%u round=%u lives=%u",static_cast<unsigned>(previous_phase),game.round(),game.lives());
            if(previous_phase==Phase::Result || previous_phase==Phase::GameOver) {
                audio.play(game.stats().passed?9:10);
                const auto& s=game.stats();
                ESP_LOGI(TAG,"Result: %s accuracy=%d.%d%% score=%d total=%d P=%u G=%u B=%u M=%u extra=%u",
                         s.passed?"PASS":"RETRY",s.accuracy/10,s.accuracy%10,s.score,game.session_score(),
                         s.perfect,s.good,s.bad,s.miss,s.stray);
            }
        }
        if(now>=next_led) {
            next_led=now+20000;
            if(!led_fault.load()) paint(now,feedback,feedback_until,locked);
            gpio_set_level(board::status,static_cast<uint32_t>((raw/(locked?100000:1000000))%2));
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
