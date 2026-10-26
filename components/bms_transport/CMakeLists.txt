idf_component_register(
    SRCS "bms_transport.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_uart
)
