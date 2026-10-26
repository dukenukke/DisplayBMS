#include "ui.h"

#include "bms_model.h"

esp_err_t ui_init(lv_display_t *disp)
{
    /* Screens are added in stages 2-3. */
    return disp ? ESP_OK : ESP_ERR_INVALID_ARG;
}
