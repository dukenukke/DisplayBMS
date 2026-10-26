idf_component_register(
    SRCS "ui.c" "fonts/ui_font_unscii_16.c" "fonts/ui_font_unscii_32.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl
    PRIV_REQUIRES board bms_model esp_system
)
# Generated font sources include "lvgl.h" directly when this is set.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
