#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "esp_lcd_ili9341.h"
#include "font_awesome_symbols.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/user_wsrgb.h"
#include "led_strip_ctl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <driver/pulse_cnt.h>
#include "hal/pcnt_types.h"
#include <wifi_station.h>

#define TAG "EspBox3Board"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

static uint8_t _ledflag = false;

UserWsrgb *led_strip_;

pcnt_unit_handle_t pcnt_unit = NULL;
QueueHandle_t queue = xQueueCreate(10, sizeof(int));

// Init ili9341 by custom cmd
static const ili9341_lcd_init_cmd_t vendor_specific_init[] = {
    {0xC8, (uint8_t []){0xFF, 0x93, 0x42}, 3, 0},
    {0xC0, (uint8_t []){0x0E, 0x0E}, 2, 0},
    {0xC5, (uint8_t []){0xD0}, 1, 0},
    {0xC1, (uint8_t []){0x02}, 1, 0},
    {0xB4, (uint8_t []){0x02}, 1, 0},
    {0xE0, (uint8_t []){0x00, 0x03, 0x08, 0x06, 0x13, 0x09, 0x39, 0x39, 0x48, 0x02, 0x0a, 0x08, 0x17, 0x17, 0x0F}, 15, 0},
    {0xE1, (uint8_t []){0x00, 0x28, 0x29, 0x01, 0x0d, 0x03, 0x3f, 0x33, 0x52, 0x04, 0x0f, 0x0e, 0x37, 0x38, 0x0F}, 15, 0},

    {0xB1, (uint8_t []){00, 0x1B}, 2, 0},
    {0x36, (uint8_t []){0x08}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0xB7, (uint8_t []){0x06}, 1, 0},

    {0x11, (uint8_t []){0}, 0x80, 0},
    {0x29, (uint8_t []){0}, 0x80, 0},

    {0, (uint8_t []){0}, 0xff, 0},
};

static bool PcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *value, void* ctx){
    BaseType_t high_task_wakeup;
    QueueHandle_t queue = (QueueHandle_t)ctx;

    //send event data to queue , from this interrupt callback
    xQueueSendFromISR(queue, &(value->watch_point_value), &high_task_wakeup);
    return (high_task_wakeup == pdTRUE);
}

static void encoder_task(void *arg)
{
    // QueueHandle_t queue = (QueueHandle_t)arg;
    int pulse_count = 0;
    int event_count = 0;

    static int last_count = 0;

    static int ledBrightnessTemp = 0;
    // ledBrightnessTemp = led_strip_->GetBrightness();

    while (1) {
        if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(100))) {
            ESP_LOGI(TAG,"Watch point event,count:%d",event_count);
        }
        else{

            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            ledBrightnessTemp = led_strip_->GetBrightness()*1000;
            if(pulse_count > last_count){

                // ESP_LOGI(TAG,"currenr brighness:%d",ledBrightnessTemp);
                if(led_strip_->GetLedPowerState()){
                    ledBrightnessTemp += 1000;
                    if(ledBrightnessTemp > 10000){
                        ledBrightnessTemp = 10000;
                    }
                    led_strip_->SetBrightness(ledBrightnessTemp/1000,2);
                }
                else{
                    
                    // ESP_LOGI(TAG,"need turn on led power.currenr brighness:%d",ledBrightnessTemp);
                }
            }
            else if(pulse_count < last_count){
                // ESP_LOGI(TAG,"direction:%d",direction);
                
                // ESP_LOGI(TAG,"currenr brighness:%d",ledBrightnessTemp);
                if(led_strip_->GetLedPowerState()){
                    ledBrightnessTemp -= 1000;
                    if(ledBrightnessTemp < 1000){
                        ledBrightnessTemp = 1000;
                    }
                    led_strip_->SetBrightness(ledBrightnessTemp/1000,2);
                }
                else{
                    
                    // ESP_LOGI(TAG,"need turn on led power.currenr brighness:%d",ledBrightnessTemp);
                }

            }
            last_count = pulse_count;

            // ESP_LOGI(TAG,"Pusle count:%d",pulse_count);
            
        }
    }
}


class EspBox3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_;
    // CircularStrip* led_strip_;
    // UserWsrgb *led_strip_;

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeEncoder(){
        pcnt_unit_config_t unit_config = {
            .low_limit = -100,
            .high_limit = 100, //后续要更改成hsv中v的范围
        };
        // pcnt_unit_handle_t pcnt_unit = NULL;
        ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

        pcnt_glitch_filter_config_t filter_config = {
            .max_glitch_ns = 1000,
        };
        ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

        pcnt_chan_config_t chan_a_config = {
            .edge_gpio_num = GPIO_NUM_16,
            .level_gpio_num = GPIO_NUM_15,
        };
        pcnt_channel_handle_t pcnt_chan_a = NULL;
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

        pcnt_chan_config_t chan_b_config = {
            .edge_gpio_num = GPIO_NUM_15,
            .level_gpio_num = GPIO_NUM_16,
        };
        pcnt_channel_handle_t pcnt_chan_b = NULL;
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));


        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a,PCNT_CHANNEL_EDGE_ACTION_DECREASE,PCNT_CHANNEL_EDGE_ACTION_INCREASE));
        ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a,PCNT_CHANNEL_LEVEL_ACTION_KEEP,PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b,PCNT_CHANNEL_EDGE_ACTION_INCREASE,PCNT_CHANNEL_EDGE_ACTION_DECREASE));
        ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b,PCNT_CHANNEL_LEVEL_ACTION_KEEP,PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

        // int watch_points[] = {-100,0,100};
        // for(size_t i = 0; i < sizeof(watch_points) / sizeof(watch_points[0]); i++){
        //     ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, watch_points[i]));
        // }
        pcnt_event_callbacks_t cbs = {
            .on_reach = PcntOnReach,
        };
        // QueueHandle_t queue = xQueueCreate(10, sizeof(int));
        ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, queue));

        ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
        ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

        xTaskCreate(encoder_task, "encoder_task", 4096, (void*)queue, 10, NULL);
    }

    // void InitializeSpi() {
    //     spi_bus_config_t buscfg = {};
    //     buscfg.mosi_io_num = GPIO_NUM_6;
    //     buscfg.miso_io_num = GPIO_NUM_NC;
    //     buscfg.sclk_io_num = GPIO_NUM_7;
    //     buscfg.quadwp_io_num = GPIO_NUM_NC;
    //     buscfg.quadhd_io_num = GPIO_NUM_NC;
    //     buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    //     ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    // }

    void InitializeButtons() {
	#if 0
        //user->config led
        gpio_config_t io_conf;
		io_conf.intr_type = GPIO_INTR_DISABLE;
		io_conf.mode = GPIO_MODE_OUTPUT;
		io_conf.pin_bit_mask = (1ULL << GPIO_NUM_40);
		io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
		io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
		gpio_config(&io_conf);
        gpio_set_level(GPIO_NUM_40, 0);
	#endif

        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
		#if 0
			if(false == _ledflag)
			{
				gpio_set_level(GPIO_NUM_40, 1);
				_ledflag = true;
			}
			else
			{
				gpio_set_level(GPIO_NUM_40, 0);
				_ledflag = false;
			}
		#endif
        });
    }
#if 0
    void InitializeIli9341Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_5;
        io_config.dc_gpio_num = GPIO_NUM_4;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        const ili9341_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(ili9341_lcd_init_cmd_t),
        };

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_48;
        panel_config.flags.reset_active_high = 1,
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void *)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
#else
                                        .emoji_font = font_emoji_64_init(),
#endif
                                    });
    }
#endif
    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        // thing_manager.AddThing(iot::CreateThing("Screen"));

		// thing_manager.AddThing(iot::CreateThing("Lamp"));
		// thing_manager.AddThing(iot::CreateThing("Wsrgb"));
        thing_manager.AddThing(iot::CreateThing("UserFPC"));

        led_strip_ = new UserWsrgb(GPIO_NUM_40,12);
        auto led_strip_ctl = new LedStripCtl(led_strip_);
        thing_manager.AddThing(led_strip_ctl);
    }

public:
    EspBox3Board() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        // InitializeSpi();
        // InitializeIli9341Display();
        InitializeEncoder();
        InitializeButtons();
        InitializeIot();
        // GetBacklight()->RestoreBrightness();
    }

	// virtual Led* GetLed() override {
	// 	static GpioLed led(GPIO_NUM_40);
	// 	return &led;
	// }


    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Led* GetLed() override {
        return led_strip_;
    }

    // virtual Led* GetLed() override {
    //     return led_strip_;
    // }

    // virtual Backlight* GetBacklight() override {
    //     static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    //     return &backlight;
    // }
};

DECLARE_BOARD(EspBox3Board);
