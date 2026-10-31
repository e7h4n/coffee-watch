idf_component_register(SRCS "main.cpp" "board.cpp" "ui.cpp" "transport.c"
 INCLUDE_DIRS "." REQUIRES bt nvs_flash esp_timer M5GFX M5PM1 M5IOE1 bookoo difluid_rs coffee_flow mbedtls)
