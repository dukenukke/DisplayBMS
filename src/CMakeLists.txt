idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES board bms_transport bms_protocol bms_model ui espressif__esp_lvgl_port
)
