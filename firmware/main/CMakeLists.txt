idf_component_register(
    SRCS
        "app_main.c"
        "ring_buffer.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        esp_timer
        mdns
        bt
        esp32-camera
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE RB_USE_FREERTOS=1)
