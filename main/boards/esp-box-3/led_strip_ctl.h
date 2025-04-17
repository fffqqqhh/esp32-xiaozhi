#ifndef LED_STRIP_CTL_H
#define LED_STRIP_CTL_H

#include "iot/thing.h"
// #include "led/circular_strip.h"
#include "led/user_wsrgb.h"

using namespace iot;

class LedStripCtl : public Thing {
    private:
        UserWsrgb* led_strip_;
        int brightness_level_; //亮度等级

        int LevelToBrightness(int level) const; //将等级转换为实际亮度
        RGBColor RGBToColor(uint8_t red, uint8_t green, uint8_t blue);//将RGB值转换为StripColor结构体
    public:
        explicit LedStripCtl(UserWsrgb* led_strip);
};

#endif // LED_STRIP_CTL_H

