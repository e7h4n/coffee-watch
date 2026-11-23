#include "sdkconfig.h"
#include "launcher_app.h"
#if CONFIG_CW_LAUNCHER_APP
#include "esp_log.h"
#include "okou_agent.h"

/* Identifies the managed native application in the USB installation package. */
static const char coffee_watch_profile[] __attribute__((used, section(".rodata_custom_desc"), aligned(4))) =
    "coffee-watch/native/stopwatch";

static const char *TAG = "launcher_app";

void cw_launcher_net_start(void)
{
    ESP_LOGI(TAG, "Starting %s", coffee_watch_profile);
    okou_config_t cfg = OKOU_CONFIG_DEFAULT();
    cfg.manage_network = true; /* the SDK's Wi-Fi manager is shared with the launcher (okl/wifis) */
    cfg.app_validates = true;  /* local boot health marks the image valid; works offline */
    esp_err_t e = okou_agent_start(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "okou_agent_start: %s (no network)", esp_err_to_name(e));
    }
}
#else
void cw_launcher_net_start(void)
{
}
#endif
