idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "."
                       REQUIRES ams_ble core ui companion nvs_flash esp_wifi esp_event esp_netif esp_hosted
                                esp_timer espressif__m5stack_tab5)
