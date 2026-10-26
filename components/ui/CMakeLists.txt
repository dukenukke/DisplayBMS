idf_component_register(
    SRCS "ui.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl
    PRIV_REQUIRES bms_model
)
