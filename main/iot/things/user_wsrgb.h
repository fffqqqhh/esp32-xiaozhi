#ifndef _USER_WSRGB_H_
#define _USER_WSRGB_H_

#include "led.h"
#include <driver/gpio.h>
#include <led_strip.h>
#include <esp_timer.h>
#include <atomic>
#include <mutex>
#include <vector>

#define DEFAULT_BRIGHTNESS          8
#define LOW_BRIGHTNESS              4

struct RGBColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

class UserWsrgb : public Led {
public:
    UserWsrgb(gpio_num_t gpio,uint8_t maxLeds);
    virtual ~UserWsrgb();

    void OnStateChanged() override;
    void SetBrightness(uint8_t defaultBrightness,uint8_t lowBrightness);
    void SetAllColor(RGBColor color);
    void SetSingleColor(uint8_t index, RGBColor color);
    void Blink(RGBColor color, int intervalMs);
    // void Breathe(RGBColor low,RGBColor high,int intervalMs);
    // void Scroll(RGBColor low,RGBColor high, int length, int intervalMs);
    void TurnOn();
    void TurnOff();

private:
    led_strip_handle_t ledStrip_;
    esp_timer_handle_t stripTimer_;
    std::function<void()> stripCallback_ = nullptr;

    uint8_t maxLeds_;
    // RGBColor colors_;
    std::vector<RGBColor> colors_;
    uint8_t defaultBrightness_ = DEFAULT_BRIGHTNESS;
    uint8_t lowBrightness_ = LOW_BRIGHTNESS;

    void StartStripTimerTask(int intervalMs, std::function<void()> callback);
};

#endif // _USER_WSRGB_H_
