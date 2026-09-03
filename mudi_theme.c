/* mudi_theme.c — all styles in one place */
#include "mudi_theme.h"

lv_style_t mudi_style_bg;
lv_style_t mudi_style_hdr;
lv_style_t mudi_style_tile;
lv_style_t mudi_style_ct;
lv_style_t mudi_style_accent;
lv_style_t mudi_style_btn;
lv_style_t mudi_style_tx;
lv_style_t mudi_style_tx2;
lv_style_t mudi_style_tx3;
lv_style_t mudi_style_track;
lv_style_t mudi_style_knob;

void mudi_theme_init(void) {
    lv_style_init(&mudi_style_bg);
    lv_style_set_bg_color(&mudi_style_bg, lv_color_make(10, 10, 10));
    lv_style_set_bg_opa(&mudi_style_bg, LV_OPA_COVER);

    lv_style_init(&mudi_style_hdr);
    lv_style_set_bg_color(&mudi_style_hdr, lv_color_make(26, 26, 26));
    lv_style_set_bg_opa(&mudi_style_hdr, LV_OPA_COVER);

    lv_style_init(&mudi_style_tile);
    lv_style_set_bg_color(&mudi_style_tile, lv_color_make(34, 34, 34));
    lv_style_set_bg_opa(&mudi_style_tile, LV_OPA_COVER);
    lv_style_set_border_color(&mudi_style_tile, lv_color_make(70, 70, 70));
    lv_style_set_border_width(&mudi_style_tile, 1);

    lv_style_init(&mudi_style_ct);
    lv_style_set_bg_color(&mudi_style_ct, lv_color_make(24, 24, 24));
    lv_style_set_bg_opa(&mudi_style_ct, LV_OPA_COVER);

    lv_style_init(&mudi_style_accent);
    lv_style_set_bg_color(&mudi_style_accent, lv_color_make(100, 100, 100));
    lv_style_set_bg_opa(&mudi_style_accent, LV_OPA_COVER);

    lv_style_init(&mudi_style_btn);
    lv_style_set_bg_color(&mudi_style_btn, lv_color_make(50, 50, 50));
    lv_style_set_bg_opa(&mudi_style_btn, LV_OPA_COVER);
    lv_style_set_border_color(&mudi_style_btn, lv_color_make(70, 70, 70));
    lv_style_set_border_width(&mudi_style_btn, 1);

    lv_style_init(&mudi_style_tx);
    lv_style_set_text_color(&mudi_style_tx, lv_color_make(235, 235, 235));

    lv_style_init(&mudi_style_tx2);
    lv_style_set_text_color(&mudi_style_tx2, lv_color_make(170, 170, 170));

    lv_style_init(&mudi_style_tx3);
    lv_style_set_text_color(&mudi_style_tx3, lv_color_make(110, 110, 110));

    lv_style_init(&mudi_style_track);
    lv_style_set_bg_color(&mudi_style_track, lv_color_make(80, 80, 80));
    lv_style_set_bg_opa(&mudi_style_track, LV_OPA_COVER);
    lv_style_set_radius(&mudi_style_track, LV_RADIUS_CIRCLE);

    lv_style_init(&mudi_style_knob);
    lv_style_set_bg_color(&mudi_style_knob, lv_color_make(200, 200, 200));
    lv_style_set_bg_opa(&mudi_style_knob, LV_OPA_COVER);
    lv_style_set_radius(&mudi_style_knob, LV_RADIUS_CIRCLE);
}

/* --- primitives --- */

lv_obj_t* mudi_obj(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t* mudi_label(lv_obj_t *parent, const char *text, int x, int y, lv_style_t *style) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    if (style) lv_obj_add_style(l, style, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

/* switch = 40x20 track + 16x16 knob */
lv_obj_t* mudi_switch(lv_obj_t *parent, int x, int y) {
    lv_obj_t *sw = lv_obj_create(parent);
    lv_obj_set_size(sw, 40, 20);
    lv_obj_set_pos(sw, x, y);
    lv_obj_add_style(sw, &mudi_style_track, 0);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *knob = lv_obj_create(sw);
    lv_obj_set_size(knob, 16, 16);
    lv_obj_set_pos(knob, 2, 2);
    lv_obj_add_style(knob, &mudi_style_knob, 0);
    lv_obj_clear_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
    return sw;
}

void mudi_switch_toggle(lv_obj_t *sw) {
    static int on = 0;
    on = !on;
    if (on) {
        lv_obj_set_style_bg_color(sw, lv_color_make(75, 195, 105), 0);
        lv_obj_set_pos(lv_obj_get_child(sw, 0), 22, 2);
    } else {
        lv_obj_set_style_bg_color(sw, lv_color_make(80, 80, 80), 0);
        lv_obj_set_pos(lv_obj_get_child(sw, 0), 2, 2);
    }
}
