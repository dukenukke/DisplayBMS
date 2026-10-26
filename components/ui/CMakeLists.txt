idf_component_register(
    SRCS "ui.c" "ui_util.c" "ui_overview.c" "ui_cells.c" "ui_alarms.c" "ui_info.c"
         "fonts/ui_font_unscii_16.c" "fonts/ui_font_unscii_32.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl
    PRIV_REQUIRES board bms_model esp_system esp_timer
)
# Generated font sources include "lvgl.h" directly when this is set.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
