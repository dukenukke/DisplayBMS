idf_component_register(
    SRCS "bms_transport.c" "bms_sniffer.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_uart esp_driver_gpio esp_timer nvs_flash bms_model bms_protocol
)
