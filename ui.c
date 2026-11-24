#include "ui.h"
#include "sys.h"
#include "oc.h"
#include "lvgl/src/extra/libs/freetype/lv_freetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>

/* ================= drivers ================= */

static int fb_fd = -1;
static unsigned short *fb_map, *fb_saved;
static size_t fb_size;

static void fb_flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c){
    for(int y = a->y1; y <= a->y2; y++)
        for(int x = a->x1; x <= a->x2; x++)
            fb_map[y*240+x] = c++->full;
    lv_disp_flush_ready(d);
}
static void disp_init(void){
    fb_fd = open("/dev/fb0", O_RDWR);
    struct fb_fix_screeninfo fi;
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fi);
    fb_size = fi.smem_len;
    fb_map = mmap(NULL, fb_size, PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, 0);
    fb_saved = malloc(fb_size);
    memcpy(fb_saved, fb_map, fb_size);
    static lv_color_t b1[240*10];
    static lv_disp_draw_buf_t db;
    lv_disp_draw_buf_init(&db, b1, NULL, 240*10);
    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.draw_buf = &db; dd.flush_cb = fb_flush;
    dd.hor_res = 240; dd.ver_res = 320;
    lv_disp_drv_register(&dd);
}
static void disp_done(void){
    memcpy(fb_map, fb_saved, fb_size);
    munmap(fb_map, fb_size);
    free(fb_saved);
    close(fb_fd);
}

static int g_tfd = -1;
static void touch_read(lv_indev_drv_t *d, lv_indev_data_t *da){
    static int lx = 120, ly = 160, pr = 0;
    struct input_event ev;
    while(read(g_tfd, &ev, sizeof ev) > 0){
        if(ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X) lx = ev.value;
        if(ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y) ly = ev.value;
        if(ev.type == EV_KEY && ev.code == BTN_TOUCH) pr = (ev.value == 1);
    }
    da->point.x = lx; da->point.y = ly;
    da->state = pr ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
static void indev_init(void){
    g_tfd = open("/dev/input/event0", O_RDONLY|O_NONBLOCK);
    struct input_event d;
    while(read(g_tfd, &d, sizeof d) > 0){}
    static lv_indev_drv_t id;
    lv_indev_drv_init(&id);
    id.type = LV_INDEV_TYPE_POINTER;
    id.read_cb = touch_read;
    lv_indev_drv_register(&id);
}

/* ================= fonts / theme ================= */

static lv_font_t *F10, *F12, *F13, *F15;
#define TTF "/etc/gl_screen/language/ttf/default_cn_medium.ttf"

static void fonts_init(void){
    lv_freetype_init(4, 8, 512*1024);
    static lv_ft_info_t i10, i12, i13, i15;
    i10.name = TTF; i10.weight = 10; i10.style = FT_FONT_STYLE_NORMAL; i10.font = NULL;
    i12.name = TTF; i12.weight = 12; i12.style = FT_FONT_STYLE_NORMAL; i12.font = NULL;
    i13.name = TTF; i13.weight = 13; i13.style = FT_FONT_STYLE_NORMAL; i13.font = NULL;
    i15.name = TTF; i15.weight = 15; i15.style = FT_FONT_STYLE_NORMAL; i15.font = NULL;
    lv_ft_font_init(&i10); F10 = i10.font;
    lv_ft_font_init(&i12); F12 = i12.font;
    lv_ft_font_init(&i13); F13 = i13.font;
    lv_ft_font_init(&i15); F15 = i15.font;
}

static struct {
    lv_color_t bg, card, border, divider, hl, text, sec, dim, faint, iconbtn, iconbd;
} TH;
static int th_light = 0;

static void theme_load(void){
    if(!th_light){
        TH.bg = lv_color_hex(0x000000); TH.card = lv_color_hex(0x141414);
        TH.border = lv_color_hex(0x2E2E2E); TH.divider = lv_color_hex(0x1F1F1F);
        TH.hl = lv_color_hex(0x1A1A1A); TH.text = lv_color_hex(0xFFFFFF);
        TH.sec = lv_color_hex(0x999999); TH.dim = lv_color_hex(0x666666);
        TH.faint = lv_color_hex(0x555555);
        TH.iconbtn = lv_color_hex(0x161616); TH.iconbd = lv_color_hex(0x3A3A3A);
    } else {
        TH.bg = lv_color_hex(0xFFFFFF); TH.card = lv_color_hex(0xF2F2F2);
        TH.border = lv_color_hex(0xDDDDDD); TH.divider = lv_color_hex(0xE8E8E8);
        TH.hl = lv_color_hex(0xE4E4E4); TH.text = lv_color_hex(0x111111);
        TH.sec = lv_color_hex(0x666666); TH.dim = lv_color_hex(0x999999);
        TH.faint = lv_color_hex(0xAAAAAA);
        TH.iconbtn = lv_color_hex(0xF5F5F5); TH.iconbd = lv_color_hex(0xCCCCCC);
    }
}

/* ================= styles ================= */

static lv_style_t s_scr, s_card, s_row, s_pill, s_iconbtn;

static void styles_init(void){
    lv_style_reset(&s_scr);
    lv_style_init(&s_scr);
    lv_style_set_bg_color(&s_scr, TH.bg);
    lv_style_set_bg_opa(&s_scr, LV_OPA_COVER);
    lv_style_set_border_width(&s_scr, 0);
    lv_style_set_radius(&s_scr, 0);
    lv_style_set_pad_all(&s_scr, 0);

    lv_style_reset(&s_card);
    lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, TH.card);
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_border_color(&s_card, TH.border);
    lv_style_set_border_width(&s_card, 1);
    lv_style_set_radius(&s_card, 8);
    lv_style_set_pad_all(&s_card, 0);

    lv_style_reset(&s_row);
    lv_style_init(&s_row);
    lv_style_set_bg_opa(&s_row, LV_OPA_TRANSP);
    lv_style_set_border_color(&s_row, TH.divider);
    lv_style_set_border_width(&s_row, 1);
    lv_style_set_border_side(&s_row, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_radius(&s_row, 0);
    lv_style_set_pad_all(&s_row, 0);

    lv_style_reset(&s_pill);
    lv_style_init(&s_pill);
    lv_style_set_bg_opa(&s_pill, LV_OPA_TRANSP);
    lv_style_set_border_color(&s_pill, TH.text);
    lv_style_set_border_width(&s_pill, 1);
    lv_style_set_radius(&s_pill, 12);
    lv_style_set_pad_all(&s_pill, 0);

    lv_style_reset(&s_iconbtn);
    lv_style_init(&s_iconbtn);
    lv_style_set_bg_color(&s_iconbtn, TH.iconbtn);
    lv_style_set_bg_opa(&s_iconbtn, LV_OPA_COVER);
    lv_style_set_border_color(&s_iconbtn, TH.iconbd);
    lv_style_set_border_width(&s_iconbtn, 1);
    lv_style_set_radius(&s_iconbtn, 6);
    lv_style_set_pad_all(&s_iconbtn, 0);
}

/* ================= helpers ================= */

static lv_obj_t *mk_obj(lv_obj_t *p, int x, int y, int w, int h, lv_style_t *st){
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    if(st) lv_obj_add_style(o, st, 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *mk_label(lv_obj_t *p, const char *t, int x, int y, lv_font_t *f, lv_color_t c){
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}
static lv_obj_t *mk_sym(lv_obj_t *p, const char *t, int x, int y, lv_color_t c){
    return mk_label(p, t, x, y, &lv_font_montserrat_14, c);
}
static lv_obj_t *mk_div(lv_obj_t *p, int x, int y, int w){
    lv_obj_t *o = mk_obj(p, x, y, w, 1, NULL);
    lv_obj_set_style_bg_color(o, TH.divider, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}
static void fmt_speed(unsigned b, char *out, int sz){
    if(b < 1024) snprintf(out, sz, "%u", b);
    else if(b < 1024*1024) snprintf(out, sz, "%u.%uK", b/1024, (b%1024)*10/1024);
    else snprintf(out, sz, "%u.%uM", b/1048576, (b%1048576)*10/1048576);
}
static void fmt_delay(int ms, char *out, int sz){
    if(ms < 0) snprintf(out, sz, "--");
    else snprintf(out, sz, "%dms", ms);
}

/* ================= persisted conf ================= */

static int show_oc = 1;

static void conf_load(void){
    FILE *f = fopen("/etc/mudi_ui.conf","r");
    if(!f) return;
    char ln[64];
    while(fgets(ln,sizeof ln,f)){
        if(strstr(ln,"show_oc=")) show_oc = atoi(strchr(ln,'=')+1);
        if(strstr(ln,"light="))   th_light = atoi(strchr(ln,'=')+1);
    }
    fclose(f);
}
static void conf_save(void){
    FILE *f = fopen("/etc/mudi_ui.conf","w");
    if(!f) return;
    fprintf(f,"show_oc=%d\nlight=%d\n", show_oc, th_light);
    fclose(f);
}

/* ================= app state ================= */

static int g_quit = 0;

static int  OC_ON = 0;
static char OC_MODE[16] = "--";
static char OC_CFG[64] = "--";
static node_t ND[64];
static int  N_ND = 0;
static char N_GRP[64] = "";
static char N_CUR[64] = "";
static int  CONN_YT = -2, CONN_GH = -2;   /* -2 unknown */
static int  SVC_BUSY = 0, CONN_BUSY = 0, TEST_BUSY = 0, CFG_BUSY = 0;
static int  CFG_BUSY_T = 0;

static void oc_load_all(void){
    if(oc_mode(OC_MODE, sizeof OC_MODE) < 0) strcpy(OC_MODE,"--");
    char names[16][64];
    if(oc_cfgs(OC_CFG, names, 16) <= 0 || !OC_CFG[0]) strcpy(OC_CFG,"--");
    N_ND = oc_nodes(N_GRP, N_CUR, ND, 64);
    if(N_ND < 0) N_ND = 0;
}

/* ================= nav ================= */

enum { PG_HOME, PG_OC, PG_NODES, PG_PICK, PG_SETTINGS };
static lv_obj_t *nav_st[8];
static int nav_pg[8];
static int nav_sp = 0;

static void build_home(void);
static void build_oc(void);
static void build_nodes(void);

static void nav_push(lv_obj_t *p, int pg){
    if(nav_sp) lv_obj_add_flag(nav_st[nav_sp-1], LV_OBJ_FLAG_HIDDEN);
    nav_st[nav_sp] = p; nav_pg[nav_sp] = pg; nav_sp++;
}
static void home_labels(void);
static void oc_labels(void);
static void nav_pop(void){
    if(nav_sp <= 1) return;
    lv_obj_del(nav_st[--nav_sp]);
    lv_obj_clear_flag(nav_st[nav_sp-1], LV_OBJ_FLAG_HIDDEN);
    int pg = nav_pg[nav_sp-1];
    if(pg == PG_HOME) home_labels();
    else if(pg == PG_OC) oc_labels();
}

static void gest_cb(lv_event_t *e){
    lv_indev_t *in = lv_indev_get_act();
    if(!in) return;
    if(lv_indev_get_gesture_dir(in) == LV_DIR_RIGHT){
        if(nav_sp > 1) nav_pop(); else g_quit = 1;
    }
}
static lv_obj_t *new_page(int pg){
    lv_obj_t *r = mk_obj(lv_scr_act(), 0, 0, 240, 320, &s_scr);
    lv_obj_add_event_cb(r, gest_cb, LV_EVENT_GESTURE, NULL);
    nav_push(r, pg);
    return r;
}

/* ================= status bar ================= */

static struct { lv_obj_t *net, *clab, *cval, *mlab, *mval, *bat, *batfill; } SB;
static lv_obj_t *cur_sb_owner = NULL;

static void sb_status_tick(void){
    if(!cur_sb_owner) return;
    unsigned u, d;
    sys_net(&u, &d);
    char b[48], uu[12], dd[12];
    fmt_speed(u, uu, sizeof uu); fmt_speed(d, dd, sizeof dd);
    snprintf(b,sizeof b,"↑%s ↓%s", uu, dd);
    lv_label_set_text(SB.net, b);
    int cpu = sys_cpu(), ram = sys_ram(), bt = sys_batt();
    snprintf(b,sizeof b,"%d%%",cpu); lv_label_set_text(SB.cval, b);
    lv_obj_set_style_text_color(SB.cval, clr_pct(cpu), 0);
    snprintf(b,sizeof b,"%d%%",ram); lv_label_set_text(SB.mval, b);
    lv_obj_set_style_text_color(SB.mval, clr_pct(ram), 0);
    if(bt < 0){
        lv_label_set_text(SB.bat, "--");
        lv_obj_set_size(SB.batfill, 0, 7);
    } else {
        snprintf(b,sizeof b,"%d%%",bt); lv_label_set_text(SB.bat, b);
        lv_obj_set_style_text_color(SB.bat, clr_batt(bt), 0);
        int w = 16*bt/100; if(w < 1) w = 1;
        lv_obj_set_size(SB.batfill, w, 7);
        lv_obj_set_style_bg_color(SB.batfill, clr_batt(bt), 0);
    }
}

static void sb_row1(lv_obj_t *pg){
    SB.net  = mk_label(pg, "↑0 ↓0", 6, 5, F10, TH.sec);
    SB.clab = mk_label(pg, "C", 84, 5, F10, TH.dim);
    SB.cval = mk_label(pg, "--", 93, 5, F10, TH.sec);
    SB.mlab = mk_label(pg, "M", 119, 5, F10, TH.dim);
    SB.mval = mk_label(pg, "--", 128, 5, F10, TH.sec);
    SB.bat  = mk_label(pg, "--", 0, 0, F10, TH.sec);
    lv_obj_align(SB.bat, LV_ALIGN_TOP_RIGHT, -32, 5);
    lv_obj_t *out = mk_obj(pg, 210, 4, 20, 11, NULL);
    lv_obj_set_style_border_color(out, TH.sec, 0);
    lv_obj_set_style_border_width(out, 1, 0);
    lv_obj_set_style_radius(out, 2, 0);
    lv_obj_set_style_bg_opa(out, LV_OPA_TRANSP, 0);
    SB.batfill = mk_obj(pg, 212, 6, 10, 7, NULL);
    lv_obj_set_style_bg_color(SB.batfill, TH.sec, 0);
    lv_obj_set_style_bg_opa(SB.batfill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(SB.batfill, 1, 0);
    lv_obj_t *nub = mk_obj(pg, 231, 7, 3, 5, NULL);
    lv_obj_set_style_bg_color(nub, TH.sec, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    mk_div(pg, 0, 20, 240);
    cur_sb_owner = pg;
}

static void exit_cb(lv_event_t *e){ (void)e; g_quit = 1; }
static void settings_cb(lv_event_t *e);
static void back_cb(lv_event_t *e){ (void)e; nav_pop(); }

static void sb_row2_home(lv_obj_t *pg){
    lv_obj_t *g = mk_obj(pg, 8, 23, 64, 30, &s_iconbtn);
    lv_obj_add_flag(g, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g, settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gi = mk_sym(g, LV_SYMBOL_SETTINGS, 0, 0, lv_color_hex(0xCCCCCC));
    lv_obj_center(gi);
    lv_obj_t *x = mk_obj(pg, 168, 23, 64, 30, &s_iconbtn);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(x, exit_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *xi = mk_sym(x, LV_SYMBOL_CLOSE, 0, 0, lv_color_hex(0xCCCCCC));
    lv_obj_center(xi);
    mk_div(pg, 0, 56, 240);
}

static void sb_row2_sub(lv_obj_t *pg, const char *title, const char *rbtn, lv_event_cb_t rcb){
    lv_obj_t *b = mk_obj(pg, 4, 23, 40, 30, NULL);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bi = mk_sym(b, LV_SYMBOL_LEFT, 6, 7, TH.text);
    (void)bi;
    mk_label(pg, title, 36, 29, F15, TH.text);
    if(rbtn){
        lv_obj_t *t = mk_obj(pg, 184, 26, 48, 24, &s_pill);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(t, rcb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *tl = mk_label(t, rbtn, 0, 0, F12, TH.text);
        lv_obj_center(tl);
    }
    mk_div(pg, 0, 56, 240);
}

/* ================= home ================= */

static struct { lv_obj_t *badge, *badge_t, *r2, *r3n, *r3d, *ytn, *ytv, *ghn, *ghv, *card, *hint; } H;

static void home_labels(void){
    if(!H.card) return;
    if(OC_ON){
        lv_obj_set_style_bg_color(H.badge, TH.text, 0);
        lv_obj_set_style_bg_opa(H.badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(H.badge, 0, 0);
        lv_label_set_text(H.badge_t, "ON");
        lv_obj_set_style_text_color(H.badge_t, th_light ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(H.r2, TH.sec, 0);
        char b[96];
        char mc[16]; snprintf(mc,sizeof mc,"%s",OC_MODE);
        if(mc[0] >= 'a' && mc[0] <= 'z') mc[0] -= 32;
        snprintf(b,sizeof b,"%s · %s", mc, OC_CFG);
        lv_label_set_text(H.r2, b);
        lv_label_set_text(H.r3n, N_CUR[0] ? N_CUR : "--");
        int dl = -1;
        for(int i = 0; i < N_ND; i++) if(!strcmp(ND[i].name, N_CUR)){ dl = ND[i].delay; break; }
        char db[16]; fmt_delay(dl, db, sizeof db);
        lv_label_set_text(H.r3d, db);
        lv_obj_set_style_text_color(H.r3d, clr_delay(dl), 0);
        if(CONN_YT != -2){ char t[16]; fmt_delay(CONN_YT, t, sizeof t); lv_label_set_text(H.ytv, t); lv_obj_set_style_text_color(H.ytv, clr_delay(CONN_YT), 0); }
        if(CONN_GH != -2){ char t[16]; fmt_delay(CONN_GH, t, sizeof t); lv_label_set_text(H.ghv, t); lv_obj_set_style_text_color(H.ghv, clr_delay(CONN_GH), 0); }
        lv_obj_clear_flag(H.ytn, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(H.ytv, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(H.ghn, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(H.ghv, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_opa(H.badge, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(H.badge, TH.faint, 0);
        lv_obj_set_style_border_width(H.badge, 1, 0);
        lv_label_set_text(H.badge_t, "OFF");
        lv_obj_set_style_text_color(H.badge_t, lv_color_hex(0x777777), 0);
        lv_obj_set_style_text_color(H.r2, lv_color_hex(0x777777), 0);
        lv_label_set_text(H.r2, "已停止");
        lv_label_set_text(H.r3n, "点按进入详情 ›");
        lv_obj_set_style_text_color(H.r3n, TH.faint, 0);
        lv_label_set_text(H.r3d, "");
        lv_obj_add_flag(H.ytn, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(H.ytv, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(H.ghn, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(H.ghv, LV_OBJ_FLAG_HIDDEN);
    }
}

static void card_cb(lv_event_t *e){ (void)e; build_oc(); }

static void build_home(void){
    lv_obj_t *pg = new_page(PG_HOME);
    sb_row1(pg);
    sb_row2_home(pg);
    if(show_oc){
        lv_obj_t *c = mk_obj(pg, 8, 64, 224, 76, &s_card);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, card_cb, LV_EVENT_CLICKED, NULL);
        H.card = c;
        mk_label(c, "OpenClash", 12, 10, F15, TH.text);
        H.badge = mk_obj(c, 172, 9, 40, 18, NULL);
        lv_obj_set_style_radius(H.badge, 9, 0);
        H.badge_t = mk_label(H.badge, "OFF", 0, 0, F10, lv_color_hex(0x777777));
        lv_obj_center(H.badge_t);
        H.r2  = mk_label(c, "--", 12, 33, F12, TH.sec);
        H.r3n = mk_label(c, "--", 12, 53, F12, TH.text);
        lv_obj_set_width(H.r3n, 96);
        lv_label_set_long_mode(H.r3n, LV_LABEL_LONG_DOT);
        H.r3d = mk_label(c, "", 112, 53, F12, TH.sec);
        H.ytn = mk_label(c, "YT", 142, 53, F10, TH.sec);
        H.ytv = mk_label(c, "--", 156, 53, F12, TH.sec);
        H.ghn = mk_label(c, "GH", 182, 53, F10, TH.sec);
        H.ghv = mk_label(c, "--", 196, 53, F12, TH.sec);
        H.hint = NULL;
    } else {
        H.card = NULL;
        H.hint = mk_label(pg, "没有可见插件\n请在设置中开启", 0, 140, F12, TH.faint);
        lv_obj_set_style_text_align(H.hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(H.hint, LV_ALIGN_TOP_MID, 0, 140);
    }
    home_labels();
}

/* ================= oc detail ================= */

static struct { lv_obj_t *sw, *ytv, *ghv, *md, *cf, *ndn, *ndd, *svc; } D;

static void sw_cb(lv_event_t *e){
    int on = lv_obj_has_state(D.sw, LV_STATE_CHECKED) ? 1 : 0;
    SVC_BUSY = 1;
    oc_job_svc_start(on);
}

static void oc_labels(void){
    if(!D.sw) return;
    if(OC_ON) lv_obj_add_state(D.sw, LV_STATE_CHECKED);
    else lv_obj_clear_state(D.sw, LV_STATE_CHECKED);
    char b[80];
    if(OC_ON){
        lv_label_set_text(D.svc, "服务");
        char mc[16]; snprintf(mc,sizeof mc,"%s",OC_MODE);
        if(mc[0] >= 'a' && mc[0] <= 'z') mc[0] -= 32;
        lv_label_set_text(D.md, mc);
        lv_label_set_text(D.cf, OC_CFG);
        lv_label_set_text(D.ndn, N_CUR[0] ? N_CUR : "--");
        int dl = -1;
        for(int i = 0; i < N_ND; i++) if(!strcmp(ND[i].name, N_CUR)){ dl = ND[i].delay; break; }
        char db[16]; fmt_delay(dl, db, sizeof db);
        lv_label_set_text(D.ndd, db);
        lv_obj_set_style_text_color(D.ndd, clr_delay(dl), 0);
        if(CONN_YT != -2){ char t[16]; fmt_delay(CONN_YT, t, sizeof t); lv_label_set_text(D.ytv, t); lv_obj_set_style_text_color(D.ytv, clr_delay(CONN_YT), 0); }
        if(CONN_GH != -2){ char t[16]; fmt_delay(CONN_GH, t, sizeof t); lv_label_set_text(D.ghv, t); lv_obj_set_style_text_color(D.ghv, clr_delay(CONN_GH), 0); }
    } else {
        snprintf(b,sizeof b,"服务"); lv_label_set_text(D.svc, b);
        lv_label_set_text(D.md, "--");
        lv_label_set_text(D.cf, "--");
        lv_label_set_text(D.ndn, "--");
        lv_label_set_text(D.ndd, "");
        lv_label_set_text(D.ytv, "--"); lv_obj_set_style_text_color(D.ytv, TH.dim, 0);
        lv_label_set_text(D.ghv, "--"); lv_obj_set_style_text_color(D.ghv, TH.dim, 0);
    }
}

static lv_obj_t *menu_row(lv_obj_t *pg, int y, const char *label, lv_event_cb_t cb, lv_obj_t **val){
    lv_obj_t *r = mk_obj(pg, 0, y, 240, 44, &s_row);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    if(cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ll = mk_label(r, label, 0, 0, F13, TH.text);
    lv_obj_align(ll, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *ch = mk_sym(r, LV_SYMBOL_RIGHT, 0, 0, TH.sec);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_t *v = mk_label(r, "--", 0, 0, F12, TH.sec);
    lv_obj_set_width(v, 150);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -22, 0);
    *val = v;
    return r;
}

static void row_mode_cb(lv_event_t *e);
static void row_cfg_cb(lv_event_t *e);
static void row_node_cb(lv_event_t *e){ (void)e; if(!OC_ON) return; build_nodes(); }

static void build_oc(void){
    lv_obj_t *pg = new_page(PG_OC);
    sb_row1(pg);
    sb_row2_sub(pg, "OpenClash", NULL, NULL);

    D.svc = mk_label(pg, "服务", 12, 77, F13, TH.text);
    D.sw = lv_switch_create(pg);
    lv_obj_remove_style_all(D.sw);
    lv_obj_set_size(D.sw, 44, 24);
    lv_obj_set_pos(D.sw, 184, 72);
    lv_obj_set_style_bg_color(D.sw, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(D.sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(D.sw, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(D.sw, lv_color_hex(0x999999), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(D.sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(D.sw, 12, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(D.sw, lv_color_hex(0x777777), LV_PART_KNOB);
    lv_obj_set_style_bg_color(D.sw, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_radius(D.sw, 12, LV_PART_KNOB);
    lv_obj_set_style_pad_all(D.sw, -2, LV_PART_KNOB);
    lv_obj_add_event_cb(D.sw, sw_cb, LV_EVENT_VALUE_CHANGED, NULL);
    mk_div(pg, 0, 104, 240);

    mk_label(pg, "YT", 12, 113, F12, TH.sec);
    D.ytv = mk_label(pg, "--", 34, 113, F12, TH.dim);
    mk_label(pg, "GH", 112, 113, F12, TH.sec);
    D.ghv = mk_label(pg, "--", 134, 113, F12, TH.dim);
    lv_obj_t *h = mk_label(pg, "5s 自动", 0, 115, F10, TH.faint);
    lv_obj_align(h, LV_ALIGN_TOP_RIGHT, -10, 115);
    mk_div(pg, 0, 136, 240);

    menu_row(pg, 136, "模式", row_mode_cb, &D.md);
    menu_row(pg, 180, "配置", row_cfg_cb, &D.cf);
    menu_row(pg, 224, "节点", row_node_cb, &D.ndn);
    D.ndd = mk_label(pg, "", 0, 0, F12, TH.sec);
    lv_obj_align(D.ndd, LV_ALIGN_TOP_RIGHT, -32, 239);
    lv_obj_align_to(D.ndn, D.ndd, LV_ALIGN_OUT_LEFT_MID, -4, 0);
    lv_obj_set_width(D.ndn, 140);

    oc_labels();
}

/* ================= nodes page ================= */

static lv_obj_t *nlist = NULL;

static void node_sel_cb(lv_event_t *e){
    const char *nm = (const char *)lv_event_get_user_data(e);
    strncpy(N_CUR, nm, sizeof(N_CUR)-1);
    oc_select(N_GRP, nm);
    nav_pop();
}

static void nodes_render(void){
    if(!nlist) return;
    lv_obj_clean(nlist);
    for(int i = 0; i < N_ND; i++){
        lv_obj_t *r = lv_obj_create(nlist);
        lv_obj_remove_style_all(r);
        lv_obj_add_style(r, &s_row, 0);
        if(!strcmp(ND[i].name, N_CUR)){
            lv_obj_set_style_bg_color(r, TH.hl, 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        }
        lv_obj_set_size(r, 240, 38);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, node_sel_cb, LV_EVENT_CLICKED, ND[i].name);
        if(!strcmp(ND[i].name, N_CUR)){
            lv_obj_t *ok = mk_sym(r, LV_SYMBOL_OK, 0, 0, TH.text);
            lv_obj_align(ok, LV_ALIGN_LEFT_MID, 8, 0);
        }
        lv_obj_t *nm = mk_label(r, ND[i].name, 0, 0, F13, TH.text);
        lv_obj_set_width(nm, 150);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 26, 0);
        char db[16]; fmt_delay(ND[i].delay, db, sizeof db);
        lv_obj_t *dl = mk_label(r, db, 0, 0, F12, ND[i].delay < 0 ? TH.dim : clr_delay(ND[i].delay));
        lv_obj_align(dl, LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static void test_cb(lv_event_t *e){
    (void)e;
    if(!OC_ON || TEST_BUSY || !N_GRP[0]) return;
    TEST_BUSY = 1;
    oc_job_test_start(N_GRP);
}

static void build_nodes(void){
    lv_obj_t *pg = new_page(PG_NODES);
    sb_row1(pg);
    sb_row2_sub(pg, "节点选择", TEST_BUSY ? "测速中" : "测速", test_cb);
    nlist = lv_obj_create(pg);
    lv_obj_remove_style_all(nlist);
    lv_obj_add_style(nlist, &s_scr, 0);
    lv_obj_set_pos(nlist, 0, 60);
    lv_obj_set_size(nlist, 240, 260);
    lv_obj_set_flex_flow(nlist, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(nlist, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(nlist, LV_DIR_VER);
    nodes_render();
}

/* ================= pick page (mode / config) ================= */

static void pick_mode_cb(lv_event_t *e){
    const char *v = (const char *)lv_event_get_user_data(e);
    char low[16];
    snprintf(low,sizeof low,"%s",v);
    if(low[0] >= 'A' && low[0] <= 'Z') low[0] += 32;
    oc_set_mode(low);
    snprintf(OC_MODE,sizeof OC_MODE,"%s",low);
    nav_pop();
}
static void pick_cfg_cb(lv_event_t *e){
    const char *v = (const char *)lv_event_get_user_data(e);
    if(strcmp(v, OC_CFG)){
        oc_cfg_switch(v);
        snprintf(OC_CFG,sizeof OC_CFG,"%s",v);
        CFG_BUSY = 1; CFG_BUSY_T = 0;
        CONN_YT = CONN_GH = -2;
        N_ND = 0; N_CUR[0] = 0;
    }
    nav_pop();
}

static void build_pick(const char *title, char items[][64], int n, const char *cur, lv_event_cb_t cb){
    lv_obj_t *pg = new_page(PG_PICK);
    sb_row1(pg);
    sb_row2_sub(pg, title, NULL, NULL);
    lv_obj_t *list = lv_obj_create(pg);
    lv_obj_remove_style_all(list);
    lv_obj_add_style(list, &s_scr, 0);
    lv_obj_set_pos(list, 0, 60);
    lv_obj_set_size(list, 240, 260);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    for(int i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_create(list);
        lv_obj_remove_style_all(r);
        lv_obj_add_style(r, &s_row, 0);
        int sel = !strcmp(items[i], cur);
        if(sel){ lv_obj_set_style_bg_color(r, TH.hl, 0); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); }
        lv_obj_set_size(r, 240, 38);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, items[i]);
        if(sel){ lv_obj_t *ok = mk_sym(r, LV_SYMBOL_OK, 0, 0, TH.text); lv_obj_align(ok, LV_ALIGN_LEFT_MID, 8, 0); }
        lv_obj_t *nm = mk_label(r, items[i], 0, 0, F13, TH.text);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 26, 0);
    }
}

static void row_mode_cb(lv_event_t *e){
    (void)e;
    if(!OC_ON) return;
    static char items[3][64] = {"Rule","Global","Direct"};
    char cur[16]; snprintf(cur,sizeof cur,"%s",OC_MODE);
    if(cur[0] >= 'a' && cur[0] <= 'z') cur[0] -= 32;
    build_pick("模式", items, 3, cur, pick_mode_cb);
}
static void row_cfg_cb(lv_event_t *e){
    (void)e;
    if(!OC_ON) return;
    static char names[16][64];
    char cur[64];
    int n = oc_cfgs(cur, names, 16);
    if(n <= 0) return;
    build_pick("配置", names, n, cur, pick_cfg_cb);
}

/* ================= settings ================= */

static void theme_pick_cb(lv_event_t *e){
    const char *v = (const char *)lv_event_get_user_data(e);
    int nl = !strcmp(v, "浅色") ? 1 : 0;
    if(nl != th_light){
        th_light = nl;
        conf_save();
        /* rebuild whole UI */
        while(nav_sp) { lv_obj_del(nav_st[--nav_sp]); }
        theme_load();
        styles_init();
        build_home();
        settings_cb(NULL);
    } else nav_pop();
}

static void show_oc_cb(lv_event_t *e){
    show_oc = lv_obj_has_state(e->target, LV_STATE_CHECKED) ? 1 : 0;
    conf_save();
}

static void theme_row_cb(lv_event_t *e){
    (void)e;
    static char items[2][64] = {"深色","浅色"};
    build_pick("主题", items, 2, th_light ? "浅色" : "深色", theme_pick_cb);
}

static void settings_cb(lv_event_t *e){
    (void)e;
    lv_obj_t *pg = new_page(PG_SETTINGS);
    sb_row1(pg);
    sb_row2_sub(pg, "设置", NULL, NULL);

    mk_label(pg, "插件显示", 12, 70, F10, TH.dim);

    mk_label(pg, "OpenClash", 12, 104, F13, TH.text);
    lv_obj_t *s1 = lv_switch_create(pg);
    lv_obj_remove_style_all(s1);
    lv_obj_set_size(s1, 44, 24);
    lv_obj_set_pos(s1, 184, 98);
    lv_obj_set_style_bg_color(s1, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s1, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s1, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s1, lv_color_hex(0x999999), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(s1, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(s1, 12, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s1, lv_color_hex(0x777777), LV_PART_KNOB);
    lv_obj_set_style_bg_color(s1, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_radius(s1, 12, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s1, -2, LV_PART_KNOB);
    if(show_oc) lv_obj_add_state(s1, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s1, show_oc_cb, LV_EVENT_VALUE_CHANGED, NULL);

    mk_div(pg, 0, 136, 240);

    lv_obj_t *r = mk_obj(pg, 0, 140, 240, 44, &s_row);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *tl = mk_label(r, "主题", 12, 0, F13, TH.text);
    lv_obj_align(tl, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *ch = mk_sym(r, LV_SYMBOL_RIGHT, 0, 0, TH.sec);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_t *tv = mk_label(r, th_light ? "浅色" : "深色", 0, 0, F12, TH.sec);
    lv_obj_align(tv, LV_ALIGN_RIGHT_MID, -22, 0);
    lv_obj_add_event_cb(r, theme_row_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *fn = mk_label(pg, "仅列出已安装的插件 · 未安装不显示", 0, 0, F10, TH.faint);
    lv_obj_align(fn, LV_ALIGN_TOP_MID, 0, 200);
}

/* ================= tick / main loop ================= */

static void on_tick(void){
    static int t = 0;
    t++;
    sb_status_tick();

    /* oc state sync every 2s */
    if(!SVC_BUSY && (t % 2 == 0)){
        int r = oc_running();
        if(r != OC_ON){
            OC_ON = r;
            if(OC_ON) oc_load_all();
            else { CONN_YT = CONN_GH = -2; }
            if(nav_pg[nav_sp-1] == PG_HOME) home_labels();
            else if(nav_pg[nav_sp-1] == PG_OC) oc_labels();
        }
    }
    /* svc job finished */
    if(SVC_BUSY && oc_job_svc_poll()){
        SVC_BUSY = 0;
        OC_ON = oc_running();
        if(OC_ON) oc_load_all();
        if(nav_pg[nav_sp-1] == PG_HOME) home_labels();
        else if(nav_pg[nav_sp-1] == PG_OC) oc_labels();
    }
    /* cfg switch finished (api back) */
    if(CFG_BUSY){
        CFG_BUSY_T++;
        if(oc_running() || CFG_BUSY_T > 30){
            CFG_BUSY = 0;
            OC_ON = 1;
            oc_load_all();
            if(nav_pg[nav_sp-1] == PG_HOME) home_labels();
            else if(nav_pg[nav_sp-1] == PG_OC) oc_labels();
        }
    }
    /* connectivity job */
    if(OC_ON && !CONN_BUSY && !CFG_BUSY && (t % 5 == 1)){
        CONN_BUSY = 1;
        oc_job_conn_start();
    }
    if(CONN_BUSY){
        int yt, gh;
        if(oc_job_conn_poll(&yt, &gh)){
            CONN_BUSY = 0;
            CONN_YT = yt; CONN_GH = gh;
            if(nav_pg[nav_sp-1] == PG_HOME) home_labels();
            else if(nav_pg[nav_sp-1] == PG_OC) oc_labels();
        }
    }
    /* delay test finished */
    if(TEST_BUSY && oc_job_test_poll()){
        TEST_BUSY = 0;
        N_ND = oc_nodes(N_GRP, N_CUR, ND, 64);
        if(N_ND < 0) N_ND = 0;
        if(nav_pg[nav_sp-1] == PG_NODES){
            nodes_render();
            /* restore 测速 button label: rebuild page header is overkill; leave label */
        }
        else if(nav_pg[nav_sp-1] == PG_OC) oc_labels();
        else if(nav_pg[nav_sp-1] == PG_HOME) home_labels();
    }
}

void ui_run(void){
    signal(SIGCHLD, SIG_IGN);
    lv_init();
    disp_init();
    indev_init();
    fonts_init();
    conf_load();
    theme_load();
    styles_init();

    OC_ON = oc_running();
    if(OC_ON) oc_load_all();

    build_home();

    int pk = open("/dev/input/event1", O_RDONLY|O_NONBLOCK);
    int ms = 0;
    while(!g_quit){
        lv_timer_handler();
        if(pk >= 0){
            struct input_event ev;
            while(read(pk, &ev, sizeof ev) > 0)
                if(ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1) g_quit = 1;
        }
        usleep(5000);
        ms += 5;
        if(ms >= 1000){ ms = 0; on_tick(); }
    }
    if(pk >= 0) close(pk);
    close(g_tfd);
    disp_done();
}
