#include "user_wsrgb.h"
#include "application.h"
#include <driver/gpio.h>
#include <esp_log.h>

#define TAG "UserWsrgb"

UserWsrgb::UserWsrgb(gpio_num_t gpio,uint8_t maxLeds) : maxLeds_(maxLeds) {
    colors_.resize(maxLeds_);

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = maxLeds_;
    
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10*1000*1000;
    led_strip_new_rmt_device(&strip_config, &rmt_config, &ledStrip_);
    led_strip_clear(ledStrip_);

    //这里还有定时器要设置，定时器的作用是什么？
    esp_timer_create_args_t strip_timer_args = {
        .callback = [](void* arg){
            auto strip = static_cast<UserWsrgb*>(arg);
            if(strip->stripCallback_!= nullptr){
                strip->stripCallback_();
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "strip_timer",
    };
    esp_timer_create(&strip_timer_args,&stripTimer_);
}

UserWsrgb::~UserWsrgb() {
    if(ledStrip_!= nullptr){
        led_strip_del(ledStrip_);
    }
    //如果有定时器的话，这里也要删除
    esp_timer_stop(stripTimer_);
}

void UserWsrgb::SetBrightness(uint8_t defaultBrightness,uint8_t lowBrightness){
    // defaultBrightness_ = defaultBrightness;
    // lowBrightness_ = lowBrightness;
#if 0
    ESP_LOGI(TAG, "SetBrightness: defaultBrightness=%d, lowBrightness=%d", defaultBrightness, lowBrightness);
    for(int i = 0; i < maxLeds_; i++){
        // colors_[i] = colors_[i]*defaultBrightness_/8;
        colors_[i].r = colors_[i].r*defaultBrightness_/8;
        colors_[i].g = colors_[i].g*defaultBrightness_/8;
        colors_[i].b = colors_[i].b*defaultBrightness_/8;
        // led_strip_set_brightness(ledStrip_, defaultBrightness_);
        led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
    }
    led_strip_refresh(ledStrip_);
#endif
    uint8_t brightChangedDire = 0;
    uint8_t brightnessDiff = 0;
    uint8_t brightnessChangeValue = 0;
    if(defaultBrightness < defaultBrightness_){
        //变暗
        brightChangedDire = 1;
        brightnessDiff = defaultBrightness_ - defaultBrightness;
    }
    else{
        //变亮
        brightChangedDire = 2;
        brightnessDiff = defaultBrightness - defaultBrightness_;
    }
    brightnessChangeValue = brightnessDiff*32;
    ESP_LOGI(TAG,"changevalue:%d",brightnessChangeValue);
    defaultBrightness_ = defaultBrightness;
    //这里只放一个灯
    RGBColor colorTemp = colors_[0];
    ESP_LOGI(TAG,"colorTemp:%d,%d,%d",colorTemp.r,colorTemp.g,colorTemp.b);
    
    for(int i=0;i<maxLeds_;i++){
        if(brightChangedDire == 2){
            if(colors_[i].r<(255-brightnessChangeValue) && colors_[i].g<(255-brightnessChangeValue) && colors_[i].b<(255-brightnessChangeValue))
            {
                colors_[i].r = colorTemp.r+brightnessChangeValue;
                colors_[i].g = colorTemp.g+brightnessChangeValue;
                colors_[i].b = colorTemp.b+brightnessChangeValue;
            }
        }else if(brightChangedDire == 1){
            if(colors_[i].r>(brightnessChangeValue) && colors_[i].g>(brightnessChangeValue) && colors_[i].b>(brightnessChangeValue)){
                colors_[i].r = colorTemp.r-brightnessChangeValue;
                colors_[i].g = colorTemp.g-brightnessChangeValue;
                colors_[i].b = colorTemp.b-brightnessChangeValue;
            }
            
        }
        
        led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
    }
    led_strip_refresh(ledStrip_);
}

void UserWsrgb::TurnOn() {
    if(colors_[1].r == 0 && colors_[1].g == 0 && colors_[1].b == 0)
    {
        for(int i=0;i<maxLeds_;i++){
            colors_[i].r = 255;
            colors_[i].g = 165;
            colors_[i].b = 0;
            led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
        }
    }
    else
    {
        for(int i=0;i<maxLeds_;i++){
            led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
        }
    }

    led_strip_refresh(ledStrip_);
}

void UserWsrgb::TurnOff() {
    led_strip_clear(ledStrip_);
}

void UserWsrgb::SetAllColor(RGBColor color){
    esp_timer_stop(stripTimer_);
    for(int i = 0; i < maxLeds_; i++){
        colors_[i] = color;
        led_strip_set_pixel(ledStrip_, i, color.r, color.g, color.b);
    }
    led_strip_refresh(ledStrip_);
}

void UserWsrgb::SetSingleColor(uint8_t index, RGBColor color){
    esp_timer_stop(stripTimer_);
    colors_[index] = color;
    led_strip_set_pixel(ledStrip_, index, color.r, color.g, color.b);
    led_strip_refresh(ledStrip_);
}

void UserWsrgb::Blink(RGBColor color, int interval_ms){
    for(int i = 0; i < maxLeds_; i++){
        colors_[i] = color;
    }
    StartStripTimerTask(interval_ms, [this]() {
        static bool on = true;
        if(on){
            for(int i = 0; i < maxLeds_; i++){
                led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
            }
            led_strip_refresh(ledStrip_);
        }
        else{
            led_strip_clear(ledStrip_);
        }
        on = !on;
    });
}

void UserWsrgb::StartStripTimerTask(int intervalMs, std::function<void()> callback){
    if(ledStrip_ == nullptr){
        return ;
    }
    
    esp_timer_stop(stripTimer_);
    stripCallback_ = callback;
    esp_timer_start_periodic(stripTimer_, intervalMs * 1000);
}

void UserWsrgb::OnStateChanged(){
    ESP_LOGI(TAG, "OnStateChanged");
}