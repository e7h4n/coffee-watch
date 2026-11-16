/* StopWatch ES8311/I2S wiring adapted from M5Stack M5StopWatch-UserDemo.
 * SPDX-License-Identifier: MIT
 */
#include "audio.h"
#include "board.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>
#include <cstdint>
static esp_codec_dev_handle_t codec;
static TaskHandle_t player;
static i2s_chan_handle_t tx;
static constexpr unsigned sample_rate=44100;
static int16_t tone[sample_rate*30/1000],silence[sample_rate/20];
static void play(void *) {
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY); // coalesce, never queue a burst of beeps
        int rc=esp_codec_dev_write(codec,tone,sizeof tone);
        if(!rc)rc=esp_codec_dev_write(codec,silence,sizeof silence);
        if(rc) {
            board_speaker_enable(false);
            ESP_LOGE("audio","chirp failed rc=%d; speaker muted",rc);
            vTaskSuspend(nullptr);
        }
        // Zero PCM outlasts the DMA buffers; the amplifier stays enabled between
        // chirps to avoid repeated power-on clicks. I2S auto-clears on starvation.
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}
bool audio_init(i2c_master_bus_handle_t bus) {
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    channel.auto_clear=true;
    i2s_std_config_t standard={};
    standard.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    standard.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
    standard.gpio_cfg.mclk=GPIO_NUM_18;standard.gpio_cfg.bclk=GPIO_NUM_17;
    standard.gpio_cfg.ws=GPIO_NUM_15;standard.gpio_cfg.dout=GPIO_NUM_21;standard.gpio_cfg.din=GPIO_NUM_NC;
    if(i2s_new_channel(&channel,&tx,nullptr)!=ESP_OK||i2s_channel_init_std_mode(tx,&standard)!=ESP_OK||i2s_channel_enable(tx)!=ESP_OK)return false;
    audio_codec_i2s_cfg_t data_cfg={};data_cfg.tx_handle=tx;
    const audio_codec_data_if_t *data=audio_codec_new_i2s_data(&data_cfg);
    audio_codec_i2c_cfg_t ctrl_cfg={};ctrl_cfg.addr=ES8311_CODEC_DEFAULT_ADDR;ctrl_cfg.bus_handle=bus;
    const audio_codec_ctrl_if_t *ctrl=audio_codec_new_i2c_ctrl(&ctrl_cfg);
    const audio_codec_gpio_if_t *gpio=audio_codec_new_gpio();
    if(!data||!ctrl||!gpio)return false;
    es8311_codec_cfg_t chip={};chip.ctrl_if=ctrl;chip.gpio_if=gpio;
    chip.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC;chip.pa_pin=GPIO_NUM_NC;chip.use_mclk=true;
    const audio_codec_if_t *device=es8311_codec_new(&chip);if(!device)return false;
    esp_codec_dev_cfg_t cfg={};cfg.dev_type=ESP_CODEC_DEV_TYPE_OUT;cfg.codec_if=device;cfg.data_if=data;
    codec=esp_codec_dev_new(&cfg);if(!codec)return false;
    esp_codec_dev_sample_info_t fs={};fs.bits_per_sample=16;fs.channel=1;fs.sample_rate=sample_rate;
    if(esp_codec_dev_open(codec,&fs)||esp_codec_dev_set_out_vol(codec,15)||esp_codec_dev_write(codec,silence,sizeof silence))return false;
    // 30 ms at 1.6 kHz; only ~3% PCM full-scale, plus low codec volume.
    // 5 ms cosine fades avoid a sharp click. Never raise volume automatically.
    for(unsigned i=0;i<sizeof tone/sizeof *tone;i++) {
        float edge=fminf(1.f,fminf(i/(sample_rate*.005f),(sizeof tone/sizeof *tone-1-i)/(sample_rate*.005f)));
        float envelope=.5f-.5f*cosf(3.14159265f*edge);
        tone[i]=(int16_t)(1000.f*envelope*sinf(2.f*3.14159265f*1600.f*i/sample_rate));
    }
    if(xTaskCreate(play,"drip_chirp",3072,nullptr,2,&player)!=pdPASS)return false;
    vTaskDelay(pdMS_TO_TICKS(80)); // silence has reached the DAC before enabling PA
    ESP_LOGI("audio","quiet 30ms chirp ready; codec volume=15");
    return true;
}
void audio_chirp() {if(player)xTaskNotifyGive(player);}
