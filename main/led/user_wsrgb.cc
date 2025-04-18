#include "user_wsrgb.h"
#include "application.h"
#include <driver/gpio.h>
#include <esp_log.h>

#define TAG "UserWsrgb"

#define BRIGHT_MAX          10000
#define BRIGHT_ATTE         1

UserWsrgb::UserWsrgb(gpio_num_t gpio,uint8_t maxLeds) : maxLeds_(maxLeds) {
    colors_.resize(maxLeds_);
    hsvColors_.resize(maxLeds_);

    for(int i=0;i<maxLeds_;i++){
        hsvColors_[i].h = 0;
        hsvColors_[i].s = 0;
        hsvColors_[i].v = 0;

        colors_[i].r = 0;
        colors_[i].g = 0;
        colors_[i].b = 0;
    }

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

/// @brief HSV转RGB算法，输入HSV值，输出RGB值
/// @param h 色度(0~359)
/// @param s 饱和度(0~255)
/// @param v 亮度(0~BRIGHT_MAX)
/// @param R 红色(0~255)
/// @param G 绿色(0~255)
/// @param B 蓝色(0~255)
void UserWsrgb::HsvToRgb(uint16_t h, uint16_t s, uint16_t v, uint8_t* R, uint8_t* G, uint8_t* B){
    float C = 0.0,X = 0.0,Y = 0.0,Z = 0.0;
    float temp_r = 0.0,temp_g = 0.0,temp_b = 0.0;
    int i = 0;
    float H,S,V;

    //屏蔽掉黑色的情况
    if(h > 359){
        h = 359;
    }

    H=(float)(h);
    S=(float)(s)/255; //把s缩放到0～1之间
    V=(float)(v*BRIGHT_ATTE)/BRIGHT_MAX; //把v缩放到0～1之间

    if(S == 0){
        temp_r = V;
        temp_g = V;
        temp_b = V;
    }else{
        H = H/60;
        i = (int)H;
        C = H - i;

        X = V * (1 - S);
        Y = V * (1 - S * C);
        Z = V * (1 - S * (1 - C));

        switch(i){
            case 0:
                temp_r = V;
                temp_g = Z;
                temp_b = X;
                break;
            case 1:
                temp_r = Y;
                temp_g = V;
                temp_b = X;
                break;
            case 2:
                temp_r = X;
                temp_g = V;
                temp_b = Z;
                break;
            case 3:
                temp_r = X;
                temp_g = Y;
                temp_b = V;
                break;
            case 4:
                temp_r = Z;
                temp_g = X;
                temp_b = V;
                break;
            case 5:
                temp_r = V;
                temp_g = X;
                temp_b = Y;
                break;
            default:
                break;
        }
    }
    
    //如果需要转换到0-255,只需要把后面的乘1000改成255即可
    //如果需要转换到0-1000，只需要把后面的乘255改成1000即可
    *R = (uint8_t)(temp_r*255);
    *G = (uint8_t)(temp_g*255);
    *B = (uint8_t)(temp_b*255);

    // ESP_LOGI(TAG,"HsvToRgb: h=%d, s=%d, v=%d, R=%d, G=%d, B=%d",h,s,v,*R,*G,*B);
}

void UserWsrgb::RgbToHsv(uint8_t R, uint8_t G, uint8_t B, uint16_t* h, uint16_t* s, uint16_t* v){
    float r = (float)R/255.0f;
    float g = (float)G/255.0f;
    float b = (float)B/255.0f;
    float max = r > g ? (r > b ? r : b) : (g > b ? g : b); //max(R,G,B)
    float min = r < g ? (r < b ? r : b) : (g < b ? g : b); //min(R,G,B)
    float delta = max - min;
    float H = 0.0f;
    float S = 0.0f;
    float V = max;

    if(delta != 0){
        if(max == r){
            H = 60*((g - b)/delta);
        }
        else if(max == g){
            H = 60*(((b - r)/delta) + 2.0f);
        }
        else if(max == b){
            H = 60*(((r - g)/delta) + 4.0f);
        }

        if(H < 0){
            H += 359.0f; //确保H为正的
        }
        
        S = max!=0 ? (delta/max) : 0;
    }

    //将H(0-359),S(0-1),V(0-1) 缩放到 (0-359),(0-255),(0,brightness_max)
    *h = (uint16_t)H;
    *s = (uint16_t)(S*255);
    *v = (uint16_t)(V*BRIGHT_MAX);

    // ESP_LOGI(TAG,"RgbToHsv: R=%d, G=%d, B=%d, h=%d, s=%d, v=%d",R,G,B,*h,*s,*v);
}

void UserWsrgb::SetBrightness(uint8_t defaultBrightness,uint8_t lowBrightness){
    defaultBrightness_ = defaultBrightness;

    for(int i=0;i<maxLeds_;i++){
        hsvColors_[i].v = defaultBrightness_*1000;

        HsvToRgb(hsvColors_[i].h,hsvColors_[i].s,hsvColors_[i].v,&colors_[i].r,&colors_[i].g,&colors_[i].b);
        led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
    }
    led_strip_refresh(ledStrip_);
}

void UserWsrgb::TurnOn(uint8_t brightness) {
    defaultBrightness_ = brightness;
    if(colors_[1].r == 0 && colors_[1].g == 0 && colors_[1].b == 0)
    {
        for(int i=0;i<maxLeds_;i++){
            colors_[i].r = 255;
            colors_[i].g = 165;
            colors_[i].b = 0;
            RgbToHsv(colors_[i].r,colors_[i].g,colors_[i].b,&hsvColors_[i].h,&hsvColors_[i].s,&hsvColors_[i].v);
            
            hsvColors_[i].v = defaultBrightness_*1000;
            HsvToRgb(hsvColors_[i].h,hsvColors_[i].s,hsvColors_[i].v,&colors_[i].r,&colors_[i].g,&colors_[i].b);

            led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
        }
    }
    else
    {
        for(int i=0;i<maxLeds_;i++){
            hsvColors_[i].v = defaultBrightness_*1000;
            HsvToRgb(hsvColors_[i].h,hsvColors_[i].s,hsvColors_[i].v,&colors_[i].r,&colors_[i].g,&colors_[i].b);
            led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
        }
    }

    led_strip_refresh(ledStrip_);
}

void UserWsrgb::TurnOff() {
    esp_timer_stop(stripTimer_);
    led_strip_clear(ledStrip_);
}

void UserWsrgb::SetAllColor(RGBColor color){
    esp_timer_stop(stripTimer_);
    for(int i = 0; i < maxLeds_; i++){
        colors_[i] = color;
        RgbToHsv(colors_[i].r,colors_[i].g,colors_[i].b,&hsvColors_[i].h,&hsvColors_[i].s,&hsvColors_[i].v);
        led_strip_set_pixel(ledStrip_, i, color.r, color.g, color.b);
    }
    led_strip_refresh(ledStrip_);
}

void UserWsrgb::SetSingleColor(uint8_t index, RGBColor color){
    esp_timer_stop(stripTimer_);
    colors_[index] = color;
    RgbToHsv(colors_[index].r,colors_[index].g,colors_[index].b,&hsvColors_[index].h,&hsvColors_[index].s,&hsvColors_[index].v);
    led_strip_set_pixel(ledStrip_, index, color.r, color.g, color.b);
    led_strip_refresh(ledStrip_);
}

void UserWsrgb::Always(){
    esp_timer_stop(stripTimer_);
    for(int i = 0; i < maxLeds_; i++){
        RgbToHsv(colors_[i].r,colors_[i].g,colors_[i].b,&hsvColors_[i].h,&hsvColors_[i].s,&hsvColors_[i].v);
        led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
    }
    led_strip_refresh(ledStrip_);
}

void UserWsrgb::Blink(RGBColor color, int interval_ms){
    for(int i = 0; i < maxLeds_; i++){
        // colors_[i] = color;
        RgbToHsv(colors_[i].r,colors_[i].g,colors_[i].b,&hsvColors_[i].h,&hsvColors_[i].s,&hsvColors_[i].v);
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

void UserWsrgb::Breathe(RGBColor color, int intervalM){
    for(int i = 0; i < maxLeds_; i++){
        // colors_[i] = color;
        RgbToHsv(colors_[i].r,colors_[i].g,colors_[i].b,&hsvColors_[i].h,&hsvColors_[i].s,&hsvColors_[i].v);
    }

    StartStripTimerTask(intervalM, [this]() {
        static bool increase = true;
        static uint16_t hsv_v_value = hsvColors_[0].v;
        if(increase){
            if(hsv_v_value < BRIGHT_MAX){
                hsv_v_value += 1000;
            }

            if(hsv_v_value >= BRIGHT_MAX){
                hsv_v_value = BRIGHT_MAX;
                increase = false;
            }
        }
        else{
            if(hsv_v_value > 2000){
                hsv_v_value -= 1000;
            }

            if(hsv_v_value <= 2000){
                hsv_v_value = 2000;
                increase = true;
            }
        }

        for(int i = 0; i < maxLeds_; i++){
            hsvColors_[i].v = hsv_v_value;
            HsvToRgb(hsvColors_[i].h,hsvColors_[i].s,hsvColors_[i].v,&colors_[i].r,&colors_[i].g,&colors_[i].b);
            led_strip_set_pixel(ledStrip_, i, colors_[i].r, colors_[i].g, colors_[i].b);
        }
        led_strip_refresh(ledStrip_);
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