/* Board display configuration adapted from M5Stack StopWatch UserDemo.
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "board.h"
#include <lgfx/v1/panel/Panel_AMOLED.hpp>
#include <M5IOE1.h>
#include <M5PM1.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstdio>
static constexpr gpio_num_t cfg_pin_sclk = GPIO_NUM_40;
static constexpr gpio_num_t cfg_pin_io0  = GPIO_NUM_41;
static constexpr gpio_num_t cfg_pin_io1  = GPIO_NUM_42;
static constexpr gpio_num_t cfg_pin_io2  = GPIO_NUM_46;
static constexpr gpio_num_t cfg_pin_io3  = GPIO_NUM_45;
static constexpr gpio_num_t cfg_pin_cs   = GPIO_NUM_39;
static constexpr gpio_num_t cfg_pin_te   = GPIO_NUM_38;
static constexpr gpio_num_t cfg_pin_rst  = GPIO_NUM_NC;

class Panel_CO5300 : public lgfx::Panel_AMOLED {
public:
    Panel_CO5300(void)
    {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
        _write_depth                           = lgfx::color_depth_t::rgb565_2Byte;
        _read_depth                            = lgfx::color_depth_t::rgb565_2Byte;
    }

    const uint8_t *getInitCommands(uint8_t listno) const override
    {
        static constexpr uint8_t list0[] = {
            0x11, 0 + CMD_INIT_DELAY,
            150,  // Sleep out
            0xC4, 1,
            0x80, 0x35,
            1,    0x80,
            0x44, 2,
            0x01, 0xD2,  // Tear Effect Line = 0x1D2 == 466
            0x53, 1,
            0x20, 0x20,
            0,    0x36,
            1,    0,
            0x51, 1,
            0xA0, 0x29,
            0,    0xff,
            0xff  // end
        };
        switch (listno) {
            case 0:
                return list0;
            default:
                return nullptr;
        }
    }
};

class M5StopWatch : public M5GFX {
    lgfx::Bus_SPI _bus_instance;
    Panel_CO5300 _panel_instance;

public:
    M5StopWatch(void)
    {
    }

    // static constexpr int in_i2c_port                   = 0;  // I2C_NUM_0

    bool init_impl(bool use_reset, bool use_clear) override
    {
        {
            auto cfg = _bus_instance.config();

            cfg.freq_write = 80000000;
            cfg.freq_read  = 10000000;  // irrelevant

            cfg.pin_sclk = cfg_pin_sclk;
            cfg.pin_io0  = cfg_pin_io0;
            cfg.pin_io1  = cfg_pin_io1;
            cfg.pin_io2  = cfg_pin_io2;
            cfg.pin_io3  = cfg_pin_io3;

            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;  // SPI_MODE0;
            cfg.spi_3wire   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg         = _panel_instance.config();
            cfg.pin_rst      = cfg_pin_rst;
            cfg.pin_cs       = cfg_pin_cs;
            cfg.panel_width  = 468;
            cfg.panel_height = 466;
            cfg.offset_x     = 6;
            cfg.offset_y     = 0;

            cfg.readable = false;

            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);

        lgfx::pinMode(cfg_pin_te, lgfx::pin_mode_t::input_pullup);
        // lgfx::i2c::init(in_i2c_port);

        // io_expander.digitalWrite(PY32_L3B_EN_PIN, 1);
        // io_expander.digitalWrite(PY32_OLED_RST_PIN, 1);

        if (!LGFX_Device::init_impl(use_reset, use_clear)) return false;

        // One explicit sprite buffer is owned by the application.

        _panel_instance.setBrightness(128);

        return true;
    }

    void setBrightness(uint8_t brightness)
    {
        _panel_instance.setBrightness(brightness);
    }
};


static M5StopWatch display;
static LGFX_Sprite canvas(&display);
static M5PM1 pmic;
static M5IOE1 ioe;
static i2c_master_dev_handle_t touch;
static void delay_ms(unsigned ms) {vTaskDelay(pdMS_TO_TICKS(ms));}
void board_init() {
    i2c_master_bus_config_t cfg={};cfg.i2c_port=I2C_NUM_0;cfg.sda_io_num=GPIO_NUM_47;cfg.scl_io_num=GPIO_NUM_48;
    cfg.clk_source=I2C_CLK_SRC_DEFAULT;cfg.glitch_ignore_cnt=7;cfg.flags.enable_internal_pullup=true;
    i2c_master_bus_handle_t bus=nullptr;ESP_ERROR_CHECK(i2c_new_master_bus(&cfg,&bus));
    configASSERT(pmic.begin(bus)==M5PM1_OK);
    configASSERT(pmic.setI2cSleepTime(0)==M5PM1_OK);
    configASSERT(pmic.wdtSet(0)==M5PM1_OK);
    pmic.ldoSetPowerHold(true);pmic.setSingleResetDisable(true);pmic.setLedEnLevel(false);
    configASSERT(ioe.begin(bus,0x4f,M5IOE1_I2C_FREQ_400K)==M5IOE1_OK);
    configASSERT(ioe.setI2cSleepTime(0)==M5IOE1_OK);
    for(int pin: {M5IOE1_PIN_8,M5IOE1_PIN_4,M5IOE1_PIN_5}) {
        m5ioe1_err_t error;ioe.pinModeWithRes(pin,OUTPUT,&error);configASSERT(error==M5IOE1_OK);
        ioe.digitalWriteWithRes(pin,1,&error);configASSERT(error==M5IOE1_OK);
    }
    delay_ms(50);
    configASSERT(display.init());display.setBrightness(160);
    canvas.setPsram(true);canvas.setColorDepth(16);
    configASSERT(canvas.createSprite(display.width(),display.height()));
    ioe.digitalWrite(M5IOE1_PIN_4,0);delay_ms(10);ioe.digitalWrite(M5IOE1_PIN_4,1);delay_ms(50);
    i2c_device_config_t tc={};tc.dev_addr_length=I2C_ADDR_BIT_LEN_7;tc.device_address=0x15;tc.scl_speed_hz=100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus,&tc,&touch));
    uint8_t reg=0xa7,id=0;ESP_ERROR_CHECK(i2c_master_transmit_receive(touch,&reg,1,&id,1,100));
    ESP_LOGI("board","display=%dx%d touch_id=0x%02x",display.width(),display.height(),id);
    for(int pin: {1,2}) {gpio_reset_pin((gpio_num_t)pin);gpio_set_direction((gpio_num_t)pin,GPIO_MODE_INPUT);gpio_set_pull_mode((gpio_num_t)pin,GPIO_PULLUP_ONLY);}
}
BoardInput board_input() {
    BoardInput r={};r.next=!gpio_get_level(GPIO_NUM_2);r.select=!gpio_get_level(GPIO_NUM_1);
    uint8_t reg=0,b[7]={};
    esp_err_t rc=i2c_master_transmit_receive(touch,&reg,1,b,sizeof b,50);
    r.touch_valid=rc==ESP_OK;
    if(rc==ESP_OK) {unsigned status=b[3]>>6;r.touch=b[2]>0&&(status==0||status==2);r.x=((b[3]&15)<<8)|b[4];r.y=((b[5]&15)<<8)|b[6];}
    return r;
}
LGFX_Sprite &board_canvas() {return canvas;}
void board_present() {canvas.pushSprite(0,0);}
void board_dump() {
    printf("SCREEN565 %d %d\n",(int)canvas.width(),(int)canvas.height());
    static const char digits[]="0123456789ABCDEF";
    char line[1900];
    for(int y=0;y<canvas.height();y++) {
        int offset=snprintf(line,sizeof line,"PX:%d:",y);
        for(int x=0;x<canvas.width();x++) {
            uint16_t pixel=canvas.readPixel(x,y);
            for(int shift=12;shift>=0;shift-=4)line[offset++]=digits[(pixel>>shift)&15];
        }
        line[offset]=0;puts(line);vTaskDelay(1);
    }
    puts("SCREEN_END");
}

void board_shutdown() {
    configASSERT(pmic.shutdown()==M5PM1_OK);
}

int board_battery_percent() {
    static uint16_t filtered_mv=0;
    uint16_t mv=0;
    if(pmic.readVbat(&mv)!=M5PM1_OK)return -1;
    filtered_mv=filtered_mv?(filtered_mv*7u+mv+4u)/8u:mv;
    if(filtered_mv<=3300)return 0;
    if(filtered_mv>=4200)return 100;
    return (filtered_mv-3300)*100u/900u;
}
