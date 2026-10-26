idf_component_register(
    SRCS "bms_model.c" "bms_alarms.c" "bms_mock.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer
)
