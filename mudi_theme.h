/* mudi_theme.h — Mudi Dashboard styles (matches gl_screen approach) */
#ifndef MUDI_THEME_H
#define MUDI_THEME_H

#include "lvgl/lvgl.h"

/* Styles */
extern lv_style_t mudi_style_bg;
extern lv_style_t mudi_style_hdr;
extern lv_style_t mudi_style_tile;
extern lv_style_t mudi_style_ct;
extern lv_style_t mudi_style_accent;
extern lv_style_t mudi_style_btn;
extern lv_style_t mudi_style_tx;
extern lv_style_t mudi_style_tx2;
extern lv_style_t mudi_style_tx3;
extern lv_style_t mudi_style_track;
extern lv_style_t mudi_style_knob;

void mudi_theme_init(void);

/* Primitive creators — only lv_obj, lv_label, custom switch */
lv_obj_t* mudi_obj(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t* mudi_label(lv_obj_t *parent, const char *text, int x, int y, lv_style_t *style);
lv_obj_t* mudi_switch(lv_obj_t *parent, int x, int y);
void      mudi_switch_toggle(lv_obj_t *sw);

#endif
