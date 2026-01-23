idf_component_register(
    SRCS "http_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_timer log esp_netif
)
