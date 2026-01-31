idf_component_register(SRCS "ota_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server esp_https_ota app_update log esp_timer esp_netif)