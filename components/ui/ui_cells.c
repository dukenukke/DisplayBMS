#include "ui_internal.h"

#include <stdio.h>

#define CELLS_SHOWN     16      /* PB1A16S: 4 x 4 grid */
#define CELLS_COLS      4
#define CELL_W          78
#define CELL_H          38
#define CELL_GAP        2
#define CELL_BORDER     2
#define CELLS_TEXT_Y    (CELLS_COLS * (CELL_H + CELL_GAP) + 6)
#define CELLS_LINE_H    20
#define CELLS_TEXT_COLS 3
#define CELLS_COL_STEP  ((320 - 2 * UI_PAD) / CELLS_TEXT_COLS)
#define CELLS_VALUE_DX  32      /* 3-char caption + gap; values up to 8 chars fit the column */

typedef struct {
    lv_obj_t *box, *index, *balance, *value;
} cell_widgets_t;

static cell_widgets_t s_cells[CELLS_SHOWN];
static lv_obj_t *s_delta, *s_avg, *s_bal, *s_min, *s_max;

static lv_obj_t *add_cell(lv_obj_t *tile, int row, int col, const char *name)
{
    int x = UI_PAD + col * CELLS_COL_STEP, y = CELLS_TEXT_Y + row * CELLS_LINE_H;
    lv_obj_t *caption = ui_label(tile, NULL, UI_COLOR_HEADING);
    lv_label_set_text(caption, name);
    lv_obj_set_pos(caption, x, y);
    lv_obj_t *value = ui_label(tile, NULL, UI_COLOR_TEXT);
    lv_obj_set_pos(value, x + CELLS_VALUE_DX, y);
    return value;
}

void ui_cells_create(lv_obj_t *tile)
{
    for (int i = 0; i < CELLS_SHOWN; ++i) {
        cell_widgets_t *c = &s_cells[i];
        c->box = lv_obj_create(tile);
        lv_obj_remove_style_all(c->box);
        lv_obj_set_size(c->box, CELL_W, CELL_H);
        lv_obj_set_pos(c->box, (i % CELLS_COLS) * (CELL_W + CELL_GAP) + 1,
                       (i / CELLS_COLS) * (CELL_H + CELL_GAP) + 2);
        lv_obj_set_style_bg_color(c->box, UI_COLOR_PANEL, 0);
        lv_obj_set_style_bg_opa(c->box, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(c->box, 3, 0);
        lv_obj_set_style_border_width(c->box, CELL_BORDER, 0);
        lv_obj_set_style_border_color(c->box, UI_COLOR_PANEL, 0);
        lv_obj_remove_flag(c->box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        c->index = ui_label(c->box, UI_FONT_SMALL, UI_COLOR_HEADING);
        lv_label_set_text_fmt(c->index, "%02d", i + 1);
        lv_obj_set_pos(c->index, 4, 4);

        c->balance = ui_label(c->box, UI_FONT_SMALL, UI_COLOR_INFO);
        lv_label_set_text(c->balance, "B");
        lv_obj_align(c->balance, LV_ALIGN_TOP_RIGHT, -4, 4);
        lv_obj_add_flag(c->balance, LV_OBJ_FLAG_HIDDEN);

        c->value = ui_label(c->box, NULL, UI_COLOR_TEXT);
        lv_label_set_text(c->value, "-.---");
        lv_obj_align(c->value, LV_ALIGN_BOTTOM_MID, 0, -3);
    }
    s_delta = add_cell(tile, 0, 0, "dV");
    s_avg = add_cell(tile, 0, 1, "Avg");
    s_bal = add_cell(tile, 0, 2, "Bal");
    s_min = add_cell(tile, 1, 0, "Min");
    s_max = add_cell(tile, 1, 1, "Max");
}

static void set_border(lv_obj_t *box, lv_color_t color)
{
    if (lv_color_eq(lv_obj_get_style_border_color(box, LV_PART_MAIN), color)) return;
    lv_obj_set_style_border_color(box, color, 0);
}

void ui_cells_update(const bms_data_t *d, bool stale)
{
    const bms_live_t *l = &d->live;
    if (!l->valid) return;

    /* Min/max come from the voltages: the BMS index fields are unverified (OPEN_QUESTIONS B5). */
    int min_i = -1, max_i = -1;
    for (int i = 0; i < CELLS_SHOWN; ++i) {
        if (!(l->cell_present_mask & (1u << i)) || !bms_cell_mv_ok(l->cell_mv[i])) continue;
        if (min_i < 0 || l->cell_mv[i] < l->cell_mv[min_i]) min_i = i;
        if (max_i < 0 || l->cell_mv[i] > l->cell_mv[max_i]) max_i = i;
    }
    /*
     * The protocol has no per-cell balancing flags, only a global state. An active
     * balancer moves charge from the highest to the lowest cell, so mark those two.
     */
    bool balancing = l->balance_state != BMS_BALANCE_OFF;

    for (int i = 0; i < CELLS_SHOWN; ++i) {
        cell_widgets_t *c = &s_cells[i];
        bool present = l->cell_present_mask & (1u << i);
        if (!present) {
            lv_obj_add_flag(c->box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(c->box, LV_OBJ_FLAG_HIDDEN);
        char buf[8];
        ui_fmt_cell(buf, sizeof(buf), l->cell_mv[i]);
        ui_label_set_text(c->value, buf);
        ui_set_text_color(c->value, ui_live(UI_COLOR_TEXT, stale));

        lv_color_t border = UI_COLOR_PANEL;
        if (!stale && min_i != max_i) {
            if (i == min_i) border = UI_COLOR_MIN_CELL;
            if (i == max_i) border = UI_COLOR_MAX_CELL;
        }
        set_border(c->box, border);

        bool mark = balancing && !stale && (i == min_i || i == max_i);
        if (mark) lv_obj_remove_flag(c->balance, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(c->balance, LV_OBJ_FLAG_HIDDEN);
    }

    char buf[12];
    ui_label_printf(s_delta, "%umV", l->cell_delta_mv);
    ui_set_text_color(s_delta, ui_delta_color(l->cell_delta_mv, stale));
    ui_fmt_cell(buf, sizeof(buf), l->cell_avg_mv);
    ui_label_printf(s_avg, "%sV", buf);
    ui_set_text_color(s_avg, ui_live(UI_COLOR_TEXT, stale));
    int bal_ma = l->balance_ma;
    int bal_abs = bal_ma < 0 ? -bal_ma : bal_ma;
    ui_label_printf(s_bal, "%s%d.%02dA", bal_ma < 0 ? "-" : "", bal_abs / 1000, bal_abs % 1000 / 10);
    ui_set_text_color(s_bal, ui_live(UI_COLOR_TEXT, stale));

    /* "07 3.280": index and voltage, coloured like the cell borders. */
    if (min_i >= 0 && max_i >= 0) {
        ui_fmt_cell(buf, sizeof(buf), l->cell_mv[min_i]);
        ui_label_printf(s_min, "%02d %s", min_i + 1, buf);
        ui_fmt_cell(buf, sizeof(buf), l->cell_mv[max_i]);
        ui_label_printf(s_max, "%02d %s", max_i + 1, buf);
    } else {
        ui_label_set_text(s_min, "--");
        ui_label_set_text(s_max, "--");
    }
    ui_set_text_color(s_min, ui_live(UI_COLOR_MIN_CELL, stale));
    ui_set_text_color(s_max, ui_live(UI_COLOR_MAX_CELL, stale));
}
