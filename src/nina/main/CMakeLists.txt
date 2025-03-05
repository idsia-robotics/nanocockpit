idf_component_register(
    SRCS "main.c" "cpx_spi.c" "cpx_wifi.c" "trace_buffer.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_event esp_netif esp_wifi lwip mdns nvs_flash perfmon
)
