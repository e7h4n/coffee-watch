#include "sdkconfig.h"
#include "launcher_app.h"
#if CONFIG_CW_LAUNCHER_APP
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "eh_agent.h"

/* The OTA packager reads the board profile and linked SDK version from the image. */
static const char coffee_watch_profile[] __attribute__((used, section(".rodata_custom_desc"), aligned(4))) =
    "coffee-watch/native/m5stack/stopwatch\0sdk=" EH_SDK_VERSION;

static const char *TAG = "launcher_app";

void cw_launcher_net_start(void)
{
    /* board_init has configured the active-low Select input. Count the hold
     * from boot, with at least half a second of observed input. */
    if (gpio_get_level(GPIO_NUM_1) == 0) {
        int64_t deadline = esp_timer_get_time() + 500000;
        if (deadline < 3000000) deadline = 3000000;
        while (gpio_get_level(GPIO_NUM_1) == 0) {
            if (esp_timer_get_time() >= deadline) {
                esp_err_t err = eh_enter_bootloader();
                ESP_LOGE(TAG, "Entering bootloader: %s", esp_err_to_name(err));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    ESP_LOGI(TAG, "Starting %s", coffee_watch_profile);
    esp_err_t e = eh_agent_start();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "eh_agent_start: %s (no network)", esp_err_to_name(e));
    }
}
#else
void cw_launcher_net_start(void)
{
}
#endif
