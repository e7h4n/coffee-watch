/*
 * esp-launcher integration (CONFIG_CW_LAUNCHER_APP). Logs, metrics and commands go to the hub
 * through the okou_agent SDK; `console <line>` feeds the existing serial console parser, so
 * everything that works over USB serial also works remotely.
 */
#include "sdkconfig.h"
#include "launcher_app.h"
#if CONFIG_CW_LAUNCHER_APP
#include <string.h>
#include "esp_log.h"
#include "okou_agent.h"

static const char *TAG = "launcher_app";

static int cmd_console(int argc, char **argv, okou_out_t *out)
{
    char line[80] = "";
    size_t n = 0;
    for (int i = 1; i < argc; i++) {
        int w = snprintf(line + n, sizeof(line) - n, "%s%s", i > 1 ? " " : "", argv[i]);
        if (w < 0 || (size_t)w >= sizeof(line) - n) {
            okou_outf(out, "line too long (max %u chars)", (unsigned)sizeof(line) - 1);
            return 1;
        }
        n += (size_t)w;
    }
    if (n == 0) {
        okou_outf(out, "usage: console <coffee-watch console command>");
        return 1;
    }
    if (!cw_console_enqueue(line)) {
        okou_outf(out, "console queue full, retry");
        return 1;
    }
    okou_outf(out, "queued: %s (output appears in the device log)", line);
    return 0;
}

static bool s_started;

void cw_launcher_net_start(void)
{
    okou_config_t cfg = OKOU_CONFIG_DEFAULT();
    cfg.manage_network = true; /* the SDK's Wi-Fi manager is shared with the launcher (okl/wifis) */
    cfg.app_validates = true;  /* local boot health marks the image valid; works offline */
    esp_err_t e = okou_agent_start(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "okou_agent_start: %s (no network)", esp_err_to_name(e));
        return;
    }
    s_started = true;
}

void cw_launcher_app_start(void)
{
    if (s_started) {
        okou_cmd_register("console", "run a coffee-watch serial console command", cmd_console);
    }
}
#else
void cw_launcher_net_start(void)
{
}
void cw_launcher_app_start(void)
{
}
#endif
