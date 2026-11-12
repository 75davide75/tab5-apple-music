idf_component_register(SRCS "companion.c" INCLUDE_DIRS "." PRIV_INCLUDE_DIRS "../../main"
                       REQUIRES core
                       PRIV_REQUIRES esp_wifi esp_event esp_netif esp_http_client json mdns nvs_flash esp_timer)
