idf_component_register(SRCS "main.cpp" "board.cpp" "ui.cpp" "transport.c" "wifi_setup.c" "drive_setup.c"
 INCLUDE_DIRS "." REQUIRES bt nvs_flash esp_timer M5GFX M5PM1 M5IOE1 bookoo difluid_rs coffee_flow mbedtls esp_wifi esp_event esp_netif wifi_config drive_config esp_http_client json brew_record)
