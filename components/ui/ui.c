#include "ui.h"
#include "ui_internal.h"

#include "bms_model.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#define UI_PAGES            4
#define UI_DOT_SIZE         6
#define UI_DOT_GAP          8

static const char *const s_titles[UI_PAGES] = {"OVERVIEW", "CELLS", "ALARMS", "INFO"};

static lv_obj_t *s_tileview;
static lv_obj_t *s_title, *s_link;
static lv_obj_t *s_dots[UI_PAGES];
static bms_data_t s_snapshot;   /* static: too large for the LVGL task stack frame */

static int active_page(void)
{
    /* Tiles are the tileview's only children, created in page order. */
    lv_obj_t *tile = lv_tileview_get_tile_active(s_tileview);
    return tile ? (int)lv_obj_get_index(tile) : 0;
}

static void page_changed_cb(lv_event_t *e)
{
    (void)e;
    int page = active_page();
    if (page < 0 || page >= UI_PAGES) return;
    ui_label_set_text(s_title, s_titles[page]);
    for (int i = 0; i < UI_PAGES; ++i) {
        lv_obj_set_style_bg_color(s_dots[i], i == page ? UI_COLOR_HEADING : UI_COLOR_STALE, 0);
    }
}

static void update_link(const bms_data_t *d, bool stale, int64_t now)
{
    const char *prefix = IS_MOCK_SOURCE ? "MOCK " : "";
    if (!d->live.valid) {
        ui_label_printf(s_link, "%sNO DATA", prefix);
        ui_set_text_color(s_link, UI_COLOR_STALE);
    } else if (stale) {
        ui_label_printf(s_link, "%sNO LINK", prefix);
        ui_set_text_color(s_link, UI_COLOR_FAULT);
    } else {
        long age = (long)((now - d->live.updated_us) / 1000000);
        ui_label_printf(s_link, "%sLINK %lds", prefix, age);
        ui_set_text_color(s_link, IS_MOCK_SOURCE ? UI_COLOR_INFO : UI_COLOR_OK);
    }
}

static void refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    bms_model_snapshot(&s_snapshot);
    int64_t now = esp_timer_get_time();
    bool stale = bms_model_live_stale(&s_snapshot, now);
    update_link(&s_snapshot, stale, now);
    ui_overview_update(&s_snapshot, stale);
    ui_cells_update(&s_snapshot, stale);
    ui_alarms_update(&s_snapshot, stale);
    ui_info_update(&s_snapshot, stale);
}

static void create_status_bar(lv_obj_t *scr)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), UI_STATUS_H);
    lv_obj_set_style_bg_color(bar, UI_COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_title = ui_label(bar, NULL, UI_COLOR_HEADING);
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, UI_PAD, 0);
    s_link = ui_label(bar, NULL, UI_COLOR_STALE);
    lv_obj_align(s_link, LV_ALIGN_RIGHT_MID, -UI_PAD, 0);
}

static void create_dots(lv_obj_t *scr)
{
    const int total_w = UI_PAGES * UI_DOT_SIZE + (UI_PAGES - 1) * UI_DOT_GAP;
    for (int i = 0; i < UI_PAGES; ++i) {
        lv_obj_t *dot = lv_obj_create(scr);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, UI_DOT_SIZE, UI_DOT_SIZE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, -total_w / 2 + UI_DOT_SIZE / 2 + i * (UI_DOT_SIZE + UI_DOT_GAP),
                     -(UI_DOTS_H - UI_DOT_SIZE) / 2);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        s_dots[i] = dot;
    }
}

esp_err_t ui_init(lv_display_t *disp)
{
    if (!disp) return ESP_ERR_INVALID_ARG;

    /* Dark default theme with the 8x16 pixel font inherited by every widget. */
    lv_theme_t *theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_TEAL),
                                              lv_palette_main(LV_PALETTE_AMBER), true, UI_FONT_TEXT);
    lv_display_set_theme(disp, theme);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    create_status_bar(scr);

    s_tileview = lv_tileview_create(scr);
    lv_obj_set_size(s_tileview, LV_PCT(100), UI_TILE_H);
    lv_obj_set_pos(s_tileview, 0, UI_STATUS_H);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tileview, page_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    static void (*const create[UI_PAGES])(lv_obj_t *) = {
        ui_overview_create, ui_cells_create, ui_alarms_create, ui_info_create,
    };
    for (int i = 0; i < UI_PAGES; ++i) {
        lv_dir_t dir = (i > 0 ? LV_DIR_LEFT : 0) | (i < UI_PAGES - 1 ? LV_DIR_RIGHT : 0);
        lv_obj_t *tile = lv_tileview_add_tile(s_tileview, i, 0, dir);
        create[i](tile);
    }

    create_dots(scr);
    page_changed_cb(NULL);
    refresh_cb(NULL);
    lv_timer_create(refresh_cb, CONFIG_DBMS_UI_REFRESH_MS, NULL);
    lv_screen_load(scr);
    return ESP_OK;
}
