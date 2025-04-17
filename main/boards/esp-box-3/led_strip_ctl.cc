#include "led_strip_ctl.h"
#include "settings.h"
#include <esp_log.h>
#include "led/user_wsrgb.h"

#define TAG "LED_STRIP_CTL"

int LedStripCtl::LevelToBrightness(int level) const {
    if(level < 0)
        level = 0;
    if(level > 8)
        level = 8;
    return (1 << level) - 1; // 2^n-1
}

RGBColor LedStripCtl::RGBToColor(uint8_t red, uint8_t green, uint8_t blue){
    if(red <= 0)
        red = 0;
    if(red >= 255)
        red = 255;
    if(green <= 0)
        green = 0;
    if(green >= 255)
        green = 255;
    if(blue <= 0)
        blue = 0;
    if(blue >= 255)
        blue = 255;

    return {static_cast<uint8_t>(red), static_cast<uint8_t>(green), static_cast<uint8_t>(blue)};
}

LedStripCtl::LedStripCtl(UserWsrgb* led_strip)
    : Thing("LedStripCtl","LED灯带控制,一共有12颗灯珠"),led_strip_(led_strip){

        //从设置中读取亮度等级
        Settings settings("led_strip");
        brightness_level_ = settings.GetInt("brightness", 4); //默认等级4
        led_strip_->SetBrightness(brightness_level_,4);

        //定时设备的属性
        properties_.AddNumberProperty("brightness", "对话时的亮度等级(1-8)", [this]()->int {
            return brightness_level_;
        });

        //定义设备可以被远程执行的指令
        methods_.AddMethod("SetBrightness", "设置亮度等级(1-8)", ParameterList({
            Parameter("level", "亮度等级(1-8)", kValueTypeNumber ,true)
        }),[this](const ParameterList& parameters){
            int level = static_cast<int>(parameters["level"].number());
            ESP_LOGI(TAG, "Set LedStrip Brightness level to %d", level);

            if(level < 1)
                level = 1;
            if(level > 8)
                level = 8;
            
            brightness_level_ = level;
            // led_strip_->SetBrightness(LevelToBrightness(brightness_level_),4);
            led_strip_->SetBrightness(brightness_level_,4);

            //保存设置
            Settings settings("led_strip",true);
            settings.SetInt("brightness", brightness_level_);
        });

        methods_.AddMethod("TurnOn","打开氛围灯", ParameterList(), [this](const ParameterList& parameters){
            ESP_LOGI(TAG, "Turn On LedStrip");
            led_strip_->TurnOn();
        });

        methods_.AddMethod("TurnOff","关闭氛围灯", ParameterList(), [this](const ParameterList& parameters){
            ESP_LOGI(TAG, "Turn Off LedStrip");
            led_strip_->TurnOff();
        });
           
        methods_.AddMethod("SetSingleColor", "设置单个灯颜色", ParameterList({
            Parameter("index","灯珠索引(0-11)",kValueTypeNumber,true),
            Parameter("red","红色(0-255)",kValueTypeNumber,true),
            Parameter("green","绿色(0-255)",kValueTypeNumber,true),
            Parameter("blue","蓝色(0-255)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters){
            int index = parameters["index"].number();
            RGBColor color = RGBToColor(
                parameters["red"].number(),
                parameters["green"].number(),
                parameters["blue"].number()
            );
            ESP_LOGI(TAG, "Set LedStrip %d color to %d,%d,%d", index, color.r, color.g, color.b);
            led_strip_->SetSingleColor(index, color);
        });

        methods_.AddMethod("SetAllColor", "设置所有灯珠颜色", ParameterList({
            Parameter("red","红色(0-255)",kValueTypeNumber,true),
            Parameter("green","绿色(0-255)",kValueTypeNumber,true),
            Parameter("blue","蓝色(0-255)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters){
            RGBColor color = RGBToColor(
                parameters["red"].number(),
                parameters["green"].number(),
                parameters["blue"].number()
            );
            ESP_LOGI(TAG, "Set LedStrip all colors to %d,%d,%d", color.r, color.g, color.b);
            // for(int i=0;i<8;i++){
            //     led_strip_.SetSingleColor(i, color);
            // }
            led_strip_->SetAllColor(color);
        });

        methods_.AddMethod("Blink","闪烁动画", ParameterList({
            Parameter("red","红色(0-255)",kValueTypeNumber,true),
            Parameter("green","绿色(0-255)",kValueTypeNumber,true),
            Parameter("blue","蓝色(0-255)",kValueTypeNumber,true),
            Parameter("interval","间隔时间(ms)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters){
            int interval = parameters["interval"].number();
            RGBColor color = RGBToColor(
                parameters["red"].number(),
                parameters["green"].number(),
                parameters["blue"].number()
            );
            ESP_LOGI(TAG, "Blink LedStrip with color %d,%d,%d, interval %d ms", color.r, color.g, color.b, interval);
            led_strip_->Blink(color, interval);
        });
#if 0
        methods_.AddMethod("Scroll","跑马灯动画", ParameterList({
            Parameter("red","红色(0-255)",kValueTypeNumber,true),
            Parameter("green","绿色(0-255)",kValueTypeNumber,true),
            Parameter("blue","蓝色(0-255)",kValueTypeNumber,true),
            Parameter("length","滚动条长度(1-7)",kValueTypeNumber,true),
            Parameter("interval","间隔时间(ms)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters){
            int interval = parameters["interval"].number();
            int length = parameters["length"].number();
            RGBColor low = RGBToColor(4,4,4);
            RGBColor high = RGBToColor(
                parameters["red"].number(),
                parameters["green"].number(),
                parameters["blue"].number()
            );
            ESP_LOGI(TAG, "Scroll LedStrip with color %d,%d,%d, length %d, interval %d ms", high.red, high.green, high.blue, length, interval);
            led_strip_->Scroll(low, high, length, interval);
        });
#endif
    }
