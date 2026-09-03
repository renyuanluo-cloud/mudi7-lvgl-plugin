/*
 * core_ui.c — Mudi Dashboard Core 渲染层
 * 唯一接触 LVGL 的地方。插件通过协议 v2 声明对象, 这里负责用
 * LVGL 标准控件按页面模板渲染。插件不可见、不可控此处任何细节。
 */
#include "core.h"
#include "sys.h"
#include "mudi_tick.h"
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
static volatile int g_swipe_back = 0;
static volatile int g_dragging = 0;   /* 拖拽中 */
static volatile int g_drag_done = 0;  /* 拖拽刚结束, 下一次调用清除 */

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
    static int sx, sy;
    static int dragged = 0;
    static struct timespec t0;
    struct input_event ev;
    if(g_drag_done){ g_drag_done = 0; g_dragging = 0; }  /* 上次拖拽刚结束, 现在清除 */
    while(read(g_tfd, &ev, sizeof ev) > 0){
        if(ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X) lx = ev.value;
        if(ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y) ly = ev.value;
        if(ev.type == EV_KEY && ev.code == BTN_TOUCH){
            if(ev.value == 1){
                sx = lx; sy = ly; dragged = 0;
                clock_gettime(CLOCK_MONOTONIC, &t0);
            }
            else if(dragged){ lx = 9999; ly = 9999; }  /* 滑动后释放: 移到屏外, 不产生任何点击 */
            pr = (ev.value == 1);
        }
    }
    if(pr && !dragged){
        int dx = lx - sx, dy = ly - sy;
        if(dx*dx + dy*dy > 20*20){ dragged = 1; g_dragging = 1; }
    }
    if(!pr && dragged){ g_drag_done = 1; }  /* 拖拽刚结束, 标记下次清除 */
    da->point.x = lx; da->point.y = ly;  /* 正常上报, 滚动不受影响 */
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
    /* 该 TTF line_height=2.0em 导致文本下沉一倍字号。
       LVGL freetype 绘制链: gpos.y = pos.y + (line_height - base_line) - glyph_top
       按 ftprobe2 实测 glyph_top/bottom (9/2, 11/3, 12/3, 13/3) 收紧 lh/bl,
       保证顶留白 1-2px 且不裁字 */
    if(F10){ F10->line_height = 13; F10->base_line = 3; }
    if(F12){ F12->line_height = 15; F12->base_line = 3; }
    if(F13){ F13->line_height = 16; F13->base_line = 3; }
    if(F15){ F15->line_height = 18; F15->base_line = 3; }
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

static lv_color_t clr_of(char c){
    switch(c){
        case 'g': return lv_color_hex(0x3FB950);
        case 'y': return lv_color_hex(0xE3B341);
        case 'o': return lv_color_hex(0xF0883E);
        case 'r': return lv_color_hex(0xF85149);
        case 's': return TH.sec;
        case 'd': return TH.dim;
        case 'f': return TH.faint;
        default:  return TH.text;
    }
}

/* ================= styles ================= */

static lv_style_t s_scr, s_card, s_row, s_pill, s_iconbtn;

static void styles_init(void){
    lv_style_reset(&s_scr); lv_style_init(&s_scr);
    lv_style_set_bg_color(&s_scr, TH.bg);
    lv_style_set_bg_opa(&s_scr, LV_OPA_COVER);
    lv_style_set_border_width(&s_scr, 0);
    lv_style_set_radius(&s_scr, 0);
    lv_style_set_pad_all(&s_scr, 0);

    lv_style_reset(&s_card); lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, TH.card);
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_border_color(&s_card, TH.border);
    lv_style_set_border_width(&s_card, 1);
    lv_style_set_radius(&s_card, 8);
    lv_style_set_pad_all(&s_card, 0);

    lv_style_reset(&s_row); lv_style_init(&s_row);
    lv_style_set_bg_opa(&s_row, LV_OPA_TRANSP);
    lv_style_set_border_color(&s_row, TH.divider);
    lv_style_set_border_width(&s_row, 1);
    lv_style_set_border_side(&s_row, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_radius(&s_row, 0);
    lv_style_set_pad_all(&s_row, 0);

    lv_style_reset(&s_pill); lv_style_init(&s_pill);
    lv_style_set_bg_opa(&s_pill, LV_OPA_TRANSP);
    lv_style_set_border_color(&s_pill, TH.text);
    lv_style_set_border_width(&s_pill, 1);
    lv_style_set_radius(&s_pill, 12);
    lv_style_set_pad_all(&s_pill, 0);

    lv_style_reset(&s_iconbtn); lv_style_init(&s_iconbtn);
    lv_style_set_bg_color(&s_iconbtn, TH.iconbtn);
    lv_style_set_bg_opa(&s_iconbtn, LV_OPA_COVER);
    lv_style_set_border_color(&s_iconbtn, TH.iconbd);
    lv_style_set_border_width(&s_iconbtn, 1);
    lv_style_set_radius(&s_iconbtn, 6);
    lv_style_set_pad_all(&s_iconbtn, 0);
}

/* ================= widget kit ================= */

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
static void style_switch(lv_obj_t *sw){
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, 44, 24);
    /* Track */
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, 12, LV_PART_MAIN);
    /* Indicator: gray OFF, bright green ON */
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x555555), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x3FB950), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(sw, 12, LV_PART_INDICATOR);
    /* Knob: white always, 20x20 centered in 24px track */
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(sw, 10, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, 2, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sw, 0, LV_PART_KNOB);
}
static void fmt_speed(unsigned b, char *out, int sz){
    if(b < 1024) snprintf(out, sz, "%u", b);
    else if(b < 1024*1024) snprintf(out, sz, "%u.%uK", b/1024, (b%1024)*10/1024);
    else snprintf(out, sz, "%u.%uM", b/1048576, (b%1048576)*10/1048576);
}

/* ================= conf (theme) ================= */

static void conf_load_theme(void){
    FILE *f = fopen("/etc/mudi_ui.conf","r");
    if(!f) return;
    char ln[96];
    while(fgets(ln,sizeof ln,f)){
        char *eq = strchr(ln, '=');
        if(!eq) continue;
        char *key_end = eq;
        /* 解析 key (去除前导空格) */
        char *key = ln;
        while(key < key_end && (*key == ' ' || *key == '\t')) key++;
        if(!strncmp(key, "light", 5) && (key[5] == ' ' || key[5] == '\t' || key[5] == '=')) th_light = atoi(eq+1);
        if(!strncmp(key, "show_", 5)){
            /* show_<id>=0|1 handled in registry_load */
        }
    }
    fclose(f);
}
static void conf_save_all(void){
    FILE *f = fopen("/etc/mudi_ui.conf","w");
    if(!f) return;
    fprintf(f, "light=%d\n", th_light);
    for(int i = 0; i < N_PL; i++)
        fprintf(f, "show_%s=%d\n", PL[i].m.id, PL[i].visible);
    fclose(f);
}

/* ================= nav ================= */

static lv_obj_t *list_cont = NULL;

enum { PG_HOME, PG_DETAIL, PG_LIST, PG_SETTINGS, PG_PICK };
static struct { lv_obj_t *page; int type, a, b; } NAV[8];
static int nav_sp = 0;
static int g_quit = 0;
/* 延迟导航: 事件回调不能直接调 nav_pop/nav_quit (在执行 LVGL 事件链中删除 widget 是 UB) */
static int  g_defer_pop = 0;      /* 0=无, 1=pop, 2=quit */
static int  g_defer_quit = 0;

static void build_home(void);
static void build_settings(void);

static void nav_close_top(void){
    int t = NAV[nav_sp-1].type, a = NAV[nav_sp-1].a, b = NAV[nav_sp-1].b;
    if(t == PG_DETAIL){
        PL[a].detail_open = 0;
        ipc_send(&PL[a], "CLOSE");
        /* 保留 lists[i].cont: pop 只隐藏详情页, 下次 open_list 复用同一页面, 防对象累积 */
        for(int i = 0; i < PL[a].n_lists; i++) PL[a].lists[i].open = 0;
        list_cont = NULL;
    } else if(t == PG_LIST){
        /* 保留 L->cont 供复用 (pop 只隐藏列表页, 下次 open_list 复用; 
           内容由 list_render 在 open_list 时 lv_obj_clean 重建) */
        PL[a].lists[b].open = 0;
        list_cont = NULL;
    }
}
/* 必须在 lv_obj_del 之后调用: widget 的 user_data 指向 pobj_t, 销毁过程中仍会访问 */
static void nav_free_top_objs(void){
    int t = NAV[nav_sp-1].type, a = NAV[nav_sp-1].a;
    if(t == PG_DETAIL){
        while(PL[a].objs){ pobj_t *n = PL[a].objs->next; free(PL[a].objs); PL[a].objs = n; }
    }
}
static void nav_pop(void){
    if(nav_sp <= 1) return;
    nav_close_top();  /* PG_LIST: 只设 open=0, 不做 lv_obj_clean(隐藏页上 clean 会崩) */
    lv_obj_add_flag(NAV[nav_sp-1].page, LV_OBJ_FLAG_HIDDEN);
    NAV[nav_sp-1].page = NULL;
    nav_sp--;
    lv_obj_clear_flag(NAV[nav_sp-1].page, LV_OBJ_FLAG_HIDDEN);
}
static void nav_push(lv_obj_t *page, int type, int a, int b){
    if(nav_sp) lv_obj_add_flag(NAV[nav_sp-1].page, LV_OBJ_FLAG_HIDDEN);
    NAV[nav_sp].page = page; NAV[nav_sp].type = type; NAV[nav_sp].a = a; NAV[nav_sp].b = b;
    nav_sp++;
}
static lv_obj_t *new_page(void){
    return mk_obj(lv_scr_act(), 0, 0, 240, 320, &s_scr);
}
static void nav_back_or_quit(void){
    if(nav_sp > 1) nav_pop(); else g_quit = 1;
}

/* ================= BUSY 遮罩 (异步操作阻塞态, Core 级) ================= */

static lv_obj_t *busy_ov = NULL;
static int busy = 0, busy_t = 0;

static void busy_show(int on){
    if(on){
        if(busy_ov) return;
        busy = 1; busy_t = 0;
        busy_ov = mk_obj(lv_layer_top(), 0, 0, 240, 320, NULL);
        lv_obj_set_style_bg_color(busy_ov, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(busy_ov, LV_OPA_50, 0);
        lv_obj_add_flag(busy_ov, LV_OBJ_FLAG_CLICKABLE);   /* 吞掉全部触摸 */
        lv_obj_t *sp = lv_spinner_create(busy_ov, 1200, 60);
        lv_obj_set_size(sp, 34, 34);
        lv_obj_align(sp, LV_ALIGN_CENTER, 0, -16);
        lv_obj_set_style_arc_color(sp, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(sp, lv_color_hex(0x555555), LV_PART_MAIN);
        lv_obj_set_style_arc_width(sp, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(sp, 3, LV_PART_INDICATOR);
        lv_obj_t *l = mk_label(busy_ov, "处理中…", 0, 0, F13, lv_color_hex(0xDDDDDD));
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 24);
    } else {
        if(!busy_ov) return;
        lv_obj_del(busy_ov);
        busy_ov = NULL;
        busy = 0;
    }
}

/* ================= status bar ================= */

static struct { lv_obj_t *owner, *net, *clab, *cval, *mlab, *mval; } SB;

static void sb_tick(void){
    if(!SB.owner) return;
    unsigned u, d;
    sys_net(&u, &d);
    char b[48], uu[12], dd[12];
    fmt_speed(u, uu, sizeof uu); fmt_speed(d, dd, sizeof dd);
    snprintf(b,sizeof b,"↑%s ↓%s", uu, dd);
    lv_label_set_text(SB.net, b);
    int cpu = sys_cpu(), ram = sys_ram();
    snprintf(b,sizeof b,"%d%%",cpu); lv_label_set_text(SB.cval, b);
    lv_obj_set_style_text_color(SB.cval, clr_pct(cpu), 0);
    snprintf(b,sizeof b,"%d%%",ram); lv_label_set_text(SB.mval, b);
    lv_obj_set_style_text_color(SB.mval, clr_pct(ram), 0);
}

static void sb_row1(lv_obj_t *pg){
    /* 状态行中线 y=10, F10 修正后盒高 13 → y=3 */
    SB.net  = mk_label(pg, "↑0 ↓0", 6, 3, F10, TH.sec);
    SB.clab = mk_label(pg, "C", 122, 3, F10, TH.dim);
    SB.cval = mk_label(pg, "--", 131, 3, F10, TH.sec);
    SB.mlab = mk_label(pg, "M", 170, 3, F10, TH.dim);
    SB.mval = mk_label(pg, "--", 179, 3, F10, TH.sec);
    /* 电池显示已移除: 原生电量 (quec_battery OCV 修正) 与 sysfs 读数差异随电量
       变化, 无法简单对齐; 顶栏只保留 网速/C/M 状态行 */
    mk_div(pg, 0, 20, 240);
    SB.owner = pg;
}

static void exit_cb(lv_event_t *e){ (void)e; if(!busy) g_defer_quit = 1; }
static void settings_cb(lv_event_t *e){ (void)e; build_settings(); }
static void back_cb(lv_event_t *e){ (void)e; g_defer_pop = 1; }

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

static void sb_row2_sub(lv_obj_t *pg, const char *title, const char *rbtn, lv_event_cb_t rcb, void *ev_ud, void *obj_ud, const char *rbtn2, lv_event_cb_t rcb2){
    lv_obj_t *b = mk_obj(pg, 4, 23, 40, 30, NULL);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bi = mk_sym(b, LV_SYMBOL_LEFT, 0, 0, TH.text);
    lv_obj_center(bi);
    /* 行动行中线 y=38, F15 修正后盒高 18 → y=29 */
    mk_label(pg, title, 36, 29, F15, TH.text);
    if(rbtn || rbtn2){
        if(rbtn){
            lv_obj_t *t = mk_obj(pg, rbtn2 ? 128 : 184, 26, 48, 24, &s_pill);
            lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(t, obj_ud);
            lv_obj_add_event_cb(t, rcb, LV_EVENT_CLICKED, ev_ud);
            lv_obj_t *tl = mk_label(t, rbtn, 0, 0, F12, TH.text);
            lv_obj_center(tl);
        }
        if(rbtn2){
            lv_obj_t *t2 = mk_obj(pg, 184, 26, 48, 24, &s_pill);
            lv_obj_add_flag(t2, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(t2, obj_ud);
            lv_obj_add_event_cb(t2, rcb2, LV_EVENT_CLICKED, ev_ud);
            lv_obj_t *tl2 = mk_label(t2, rbtn2, 0, 0, F12, TH.text);
            lv_obj_center(tl2);
        }
    }
    mk_div(pg, 0, 56, 240);
}

/* ================= detail page (template: switch区/信息区/菜单区) ================= */

static lv_obj_t *d_zsw, *d_zinfo, *d_zmenu;

static void sw_event_cb(lv_event_t *e){
    plugin_t *p = (plugin_t *)lv_event_get_user_data(e);
    pobj_t *o = (pobj_t *)lv_obj_get_user_data(e->target);
    int on = lv_obj_has_state(e->target, LV_STATE_CHECKED) ? 1 : 0;
    ipc_send(p, "EV\t%s\t%d", o->id, on);
}

static void menu_event_cb(lv_event_t *e);
static void open_list(plugin_t *p, int li);

static pobj_t *detail_add(plugin_t *p, char type, const char *id,
                          const char *label, const char *value, char color){
    pobj_t *o = calloc(1, sizeof(pobj_t));
    strncpy(o->id, id, sizeof(o->id)-1);
    o->type = type;
    o->next = p->objs;
    p->objs = o;
    if(type == 's'){
        lv_obj_t *r = lv_obj_create(d_zsw);
        lv_obj_remove_style_all(r);
        lv_obj_add_style(r, &s_row, 0);
        lv_obj_set_size(r, 240, 40);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        o->row = r;
        lv_obj_t *ll = mk_label(r, label, 0, 0, F13, TH.text);
        lv_obj_align(ll, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_t *sw = lv_switch_create(r);
        style_switch(sw);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -12, 0);
        if(value && value[0] == '1') lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_user_data(sw, o);
        lv_obj_add_event_cb(sw, sw_event_cb, LV_EVENT_VALUE_CHANGED, p);
        o->sw = sw;
    } else if(type == 'i'){
        lv_obj_t *r = lv_obj_create(d_zinfo);
        lv_obj_remove_style_all(r);
        lv_obj_add_style(r, &s_row, 0);
        lv_obj_set_size(r, 240, 32);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        o->row = r;
        lv_obj_t *ll = mk_label(r, label, 0, 0, F12, TH.sec);
        lv_obj_align(ll, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_t *v = mk_label(r, value ? value : "--", 0, 0, F12, clr_of(color));
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, -12, 0);
        o->val = v;
    } else {  /* menu */
        lv_obj_t *r = lv_obj_create(d_zmenu);
        lv_obj_remove_style_all(r);
        lv_obj_add_style(r, &s_row, 0);
        lv_obj_set_size(r, 240, 44);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r, o);
        lv_obj_add_event_cb(r, menu_event_cb, LV_EVENT_CLICKED, p);
        o->row = r;
        lv_obj_t *ll = mk_label(r, label, 0, 0, F13, TH.text);
        lv_obj_align(ll, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_t *ch = mk_sym(r, LV_SYMBOL_RIGHT, 0, 0, TH.sec);
        lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_t *v = mk_label(r, value ? value : "", 0, 0, F12, TH.sec);
        lv_obj_set_width(v, 150);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, -22, 0);
        o->val = v;
    }
    return o;
}

static pobj_t *detail_find(plugin_t *p, const char *id){
    for(pobj_t *o = p->objs; o; o = o->next)
        if(!strcmp(o->id, id)) return o;
    return NULL;
}

static void menu_event_cb(lv_event_t *e){
    if(g_dragging) return;  /* 拖拽中不响应 */
    plugin_t *p = (plugin_t *)lv_event_get_user_data(e);
    pobj_t *o = (pobj_t *)lv_obj_get_user_data(e->target);
    for(int i = 0; i < p->n_lists; i++)
        if(!strcmp(p->lists[i].id, o->id)){ open_list(p, i); return; }
    ipc_send(p, "EV\t%s\ttap", o->id);
}

/* ================= list page ================= */

/* "listid/itemid" -> 按第一个 '/' 切分 (itemid 允许空格和 '/') */
static int split_lid(const char *id, char *lid, int lsz, char *iid, int isz){
    const char *s = strchr(id, '/');
    if(!s) return -1;
    int l = (int)(s - id);
    if(l <= 0 || l >= lsz) return -1;
    memcpy(lid, id, l); lid[l] = 0;
    strncpy(iid, s + 1, isz - 1); iid[isz-1] = 0;
    return 0;
}

static void item_event_cb(lv_event_t *e){
    plugin_t *p = (plugin_t *)lv_event_get_user_data(e);
    const char *key = (const char *)lv_obj_get_user_data(e->target);
    char lid[64], iid[64];
    if(split_lid(key, lid, sizeof lid, iid, sizeof iid) == 0)
        ipc_send(p, "EV\t%s\t%s", lid, iid);
    /* 不能在 LVGL 事件处理中直接 nav_pop, 设 flag 由主循环执行 */
    g_defer_pop = 1;
}

static void list_btn_cb(lv_event_t *e){
    plugin_t *p = (plugin_t *)lv_event_get_user_data(e);
    const char *lid = (const char *)lv_obj_get_user_data(e->target);
    if(lid && p && p->alive) ipc_send(p, "EV\t%s\t@btn", lid);
}

static void list_btn2_cb(lv_event_t *e){
    plugin_t *p = (plugin_t *)lv_event_get_user_data(e);
    const char *lid = (const char *)lv_obj_get_user_data(e->target);
    if(lid && p && p->alive) ipc_send(p, "EV\t%s\t@btn2", lid);
}

static void list_render(plugin_t *p, int li){
    if(!list_cont) return;
    plist_t *L = &p->lists[li];
    lv_obj_clean(list_cont);
    for(int i = 0; i < L->n_items; i++){
        pitem_t *it = &L->items[i];
        lv_obj_t *r = lv_obj_create(list_cont);
        lv_obj_remove_style_all(r);
        lv_obj_add_style(r, &s_row, 0);
        if(it->sel){
            lv_obj_set_style_bg_color(r, TH.hl, 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        }
        lv_obj_set_size(r, 240, 38);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        static char keys[MAX_ITEMS][80];
        snprintf(keys[i], sizeof(keys[i]), "%s/%s", L->id, it->iid);
        lv_obj_set_user_data(r, keys[i]);
        lv_obj_add_event_cb(r, item_event_cb, LV_EVENT_CLICKED, p);
        if(it->sel){
            lv_obj_t *ok = mk_sym(r, LV_SYMBOL_OK, 0, 0, TH.text);
            lv_obj_align(ok, LV_ALIGN_LEFT_MID, 8, 0);
        }
        lv_obj_t *nm = mk_label(r, it->label, 0, 0, F13, TH.text);
        lv_obj_set_width(nm, 150);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 26, 0);
        if(it->sub[0]){
            lv_obj_t *dl = mk_label(r, it->sub, 0, 0, F12, clr_of(it->color));
            lv_obj_align(dl, LV_ALIGN_RIGHT_MID, -8, 0);
        }
    }
}

static void open_list(plugin_t *p, int li){
    plist_t *L = &p->lists[li];
    /* 复用已有隐藏页: 如果该 list 已经有隐藏页, 不再创建新的, 只显示并复用 */
    lv_obj_t *pg = L->page;
    if(!pg){
        pg = new_page();
        L->page = pg;
        sb_row1(pg);
        sb_row2_sub(pg, L->title, L->btn[0] ? L->btn : NULL, list_btn_cb, p, L->id, L->btn2[0] ? L->btn2 : NULL, list_btn2_cb);
        list_cont = lv_obj_create(pg);
        lv_obj_remove_style_all(list_cont);
        lv_obj_add_style(list_cont, &s_scr, 0);
        lv_obj_set_pos(list_cont, 0, 60);
        lv_obj_set_size(list_cont, 240, 260);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);
        L->cont = list_cont;        /* 关键: 保存 list_cont 引用, 复用时直接拿来用 */
    } else {
        lv_obj_clear_flag(pg, LV_OBJ_FLAG_HIDDEN);
        list_cont = L->cont;        /* 用保存的引用, 不要用 lv_obj_get_child(pg, N) */
    }
    nav_push(pg, PG_LIST, (int)(p - PL), li);
    L->open = 1;
    list_render(p, li);
}

/* ================= home ================= */

static void card_event_cb(lv_event_t *e){
    int pi = (int)(intptr_t)lv_event_get_user_data(e);
    plugin_t *p = &PL[pi];
    if(!p->alive){
        if(ipc_spawn(p) == 0) ipc_send(p, "INIT");
    }
    lv_obj_t *pg = p->detail_page;
    if(!pg){
        pg = new_page();
        p->detail_page = pg;   /* 保存复用: pop 只隐藏, 下次 open 复用同一页面 */
    } else {
        /* 复用: 先显示(可见态 clean 安全, 隐藏态 clean 会触发 LVGL 状态错误), 再整页重建内容 */
        lv_obj_clear_flag(pg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clean(pg);
    }
    sb_row1(pg);
    sb_row2_sub(pg, p->m.name, NULL, NULL, NULL, NULL, NULL, NULL);
    /* 模板: 开关区 / 信息区 / 菜单区 */
    lv_obj_t *content = lv_obj_create(pg);
    lv_obj_remove_style_all(content);
    lv_obj_add_style(content, &s_scr, 0);
    lv_obj_set_pos(content, 0, 60);
    lv_obj_set_size(content, 240, 260);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    d_zsw = lv_obj_create(content);
    lv_obj_remove_style_all(d_zsw);
    lv_obj_set_size(d_zsw, 240, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(d_zsw, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(d_zsw, LV_OBJ_FLAG_SCROLLABLE);
    mk_div(content, 0, 0, 240);
    d_zinfo = lv_obj_create(content);
    lv_obj_remove_style_all(d_zinfo);
    lv_obj_set_size(d_zinfo, 240, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(d_zinfo, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(d_zinfo, LV_OBJ_FLAG_SCROLLABLE);
    mk_div(content, 0, 0, 240);
    d_zmenu = lv_obj_create(content);
    lv_obj_remove_style_all(d_zmenu);
    lv_obj_set_size(d_zmenu, 240, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(d_zmenu, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(d_zmenu, LV_OBJ_FLAG_SCROLLABLE);
    PL[pi].detail_open = 1;
    nav_push(pg, PG_DETAIL, pi, 0);
    while(p->objs){ pobj_t *nx = p->objs->next; free(p->objs); p->objs = nx; }
    ipc_send(p, "OPEN");
}

static void card_badge(plugin_t *p){
    if(!p->badge) return;
    if(p->card_on){
        lv_obj_set_style_bg_color(p->badge, TH.text, 0);
        lv_obj_set_style_bg_opa(p->badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(p->badge, 0, 0);
        lv_label_set_text(p->badge_t, "ON");
        lv_obj_set_style_text_color(p->badge_t, th_light ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
    } else {
        lv_obj_set_style_bg_opa(p->badge, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(p->badge, TH.faint, 0);
        lv_obj_set_style_border_width(p->badge, 1, 0);
        lv_label_set_text(p->badge_t, "OFF");
        lv_obj_set_style_text_color(p->badge_t, lv_color_hex(0x777777), 0);
    }
}

static void build_home(void){
    lv_obj_t *pg = new_page();
    nav_push(pg, PG_HOME, 0, 0);
    sb_row1(pg);
    sb_row2_home(pg);
    int shown = 0;
    /* 卡片区可滚动, 适配 3+ 插件 */
    lv_obj_t *scroll = lv_obj_create(pg);
    lv_obj_remove_style_all(scroll);
    lv_obj_add_style(scroll, &s_scr, 0);
    lv_obj_set_pos(scroll, 0, 64);
    lv_obj_set_size(scroll, 240, 256);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_style_pad_row(scroll, 8, 0);
    lv_obj_set_style_pad_all(scroll, 8, 0);
    for(int i = 0; i < N_PL; i++){
        plugin_t *p = &PL[i];
        if(!p->visible) continue;
        shown++;
        lv_obj_t *c = mk_obj(scroll, 0, 0, 224, 96, &s_card);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, card_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        p->card = c;
        p->card_on = 0;
        p->r2 = NULL; p->r3l = NULL; p->r3r = NULL; p->r4 = NULL;
        mk_label(c, p->m.name, 12, 10, F15, TH.text);
        p->badge = mk_obj(c, 172, 9, 40, 18, NULL);
        lv_obj_set_style_radius(p->badge, 9, 0);
        p->badge_t = mk_label(p->badge, "OFF", 0, 0, F10, lv_color_hex(0x777777));
        lv_obj_center(p->badge_t);
        p->r2  = mk_label(c, "…", 12, 33, F12, TH.sec);
        lv_obj_set_width(p->r2, 200);
        lv_label_set_long_mode(p->r2, LV_LABEL_LONG_DOT);
        p->r3l = mk_label(c, "", 12, 53, F12, TH.sec);
        lv_obj_set_width(p->r3l, 160);
        lv_label_set_long_mode(p->r3l, LV_LABEL_LONG_DOT);
        p->r3r = mk_label(c, "", 0, 53, F12, TH.sec);
        lv_obj_align(p->r3r, LV_ALIGN_TOP_RIGHT, -10, 53);
        p->r4 = mk_label(c, "", 12, 73, F10, TH.dim);
        lv_obj_set_width(p->r4, 200);
        lv_label_set_long_mode(p->r4, LV_LABEL_LONG_DOT);
        card_badge(p);
    }
    if(!shown){
        lv_obj_t *h = mk_label(pg, "没有可见插件\n请在设置中开启", 0, 140, F12, TH.faint);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 140);
    }
}

/* ================= settings ================= */

static void vis_sw_cb(lv_event_t *e){
    int pi = (int)(intptr_t)lv_event_get_user_data(e);
    PL[pi].visible = lv_obj_has_state(e->target, LV_STATE_CHECKED) ? 1 : 0;
    registry_save_visible();
}

static void theme_row_cb(lv_event_t *e);

static void build_settings(void){
    lv_obj_t *pg = new_page();
    nav_push(pg, PG_SETTINGS, 0, 0);
    sb_row1(pg);
    sb_row2_sub(pg, "设置", NULL, NULL, NULL, NULL, NULL, NULL);
    mk_label(pg, "插件显示", 12, 70, F10, TH.dim);
    int y = 88;
    for(int i = 0; i < N_PL; i++){
        lv_obj_t *l = mk_label(pg, PL[i].m.name, 12, y + 22, F13, TH.text);
        (void)l;
        lv_obj_t *sw = lv_switch_create(pg);
        style_switch(sw);
        lv_obj_set_pos(sw, 184, y + 14);
        if(PL[i].visible) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, vis_sw_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
        y += 48;
    }
    mk_div(pg, 0, y + 8, 240);
    lv_obj_t *r = mk_obj(pg, 0, y + 12, 240, 44, &s_row);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *tl = mk_label(r, "主题", 0, 0, F13, TH.text);
    lv_obj_align(tl, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *ch = mk_sym(r, LV_SYMBOL_RIGHT, 0, 0, TH.sec);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_t *tv = mk_label(r, th_light ? "浅色" : "深色", 0, 0, F12, TH.sec);
    lv_obj_align(tv, LV_ALIGN_RIGHT_MID, -22, 0);
    lv_obj_add_event_cb(r, theme_row_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *fn = mk_label(pg, "仅列出已安装的插件 · 未安装不显示", 0, 0, F10, TH.faint);
    lv_obj_align(fn, LV_ALIGN_TOP_MID, 0, y + 66);
}

/* 通用单选子页 (设置项用) */
typedef void (*core_pick_fn)(int);
static core_pick_fn pick_fn = NULL;

static void pick_item_cb(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    core_pick_fn f = pick_fn;
    nav_pop();
    if(f) f(i);
}
static void core_pick(const char *title, const char **names, int n, int sel, core_pick_fn cb){
    pick_fn = cb;
    lv_obj_t *pg = new_page();
    nav_push(pg, PG_PICK, 0, 0);
    sb_row1(pg);
    sb_row2_sub(pg, title, NULL, NULL, NULL, NULL, NULL, NULL);
    for(int i = 0; i < n; i++){
        lv_obj_t *r = mk_obj(pg, 0, 64 + i*44, 240, 44, &s_row);
        if(i == sel){ lv_obj_set_style_bg_color(r, TH.hl, 0); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); }
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, pick_item_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        if(i == sel){ lv_obj_t *ok = mk_sym(r, LV_SYMBOL_OK, 0, 0, TH.text); lv_obj_align(ok, LV_ALIGN_LEFT_MID, 8, 0); }
        lv_obj_t *nm = mk_label(r, names[i], 0, 0, F13, TH.text);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 26, 0);
    }
}

static void theme_apply(int i){
    if(i == th_light) return;
    th_light = i;
    conf_save_all();
    while(nav_sp) nav_pop();
    theme_load();
    styles_init();
    build_home();
    build_settings();
}
static void theme_row_cb(lv_event_t *e){
    (void)e;
    static const char *names[2] = {"深色","浅色"};
    core_pick("主题", names, 2, th_light, theme_apply);
}

/* ================= protocol v2 handler ================= */

static void proto_line(plugin_t *p, char *ln){
    char *verb = strsep(&ln, "\t");
    if(!verb) return;
    if(!strcmp(verb, "BUSY")){
        busy_show(ln && ln[0] == '1');
    } else if(!strcmp(verb, "HELLO")){
        char *ver = strsep(&ln, "\t");
        char *name = ln;
        (void)ver;
        if(name && name[0]){
            strncpy(p->m.name, name, sizeof(p->m.name)-1);
            if(p->card){
                lv_obj_t *t = lv_obj_get_child(p->card, 0);
                if(t) lv_label_set_text(t, p->m.name);
            }
        }
    } else if(!strcmp(verb, "CARD")){
        p->card_on = (ln && !strcmp(ln, "ON")) ? 1 : 0;
        card_badge(p);
    } else if(!strcmp(verb, "CF")){
        char *row = strsep(&ln, "\t");
        char *text = strsep(&ln, "\t");
        char *color = ln;
        lv_obj_t *w = NULL;
        if(row && !strcmp(row, "r2")) w = p->r2;
        else if(row && !strcmp(row, "r3l")) w = p->r3l;
        else if(row && !strcmp(row, "r3r")) w = p->r3r;
        else if(row && !strcmp(row, "r4"))  w = p->r4;
        if(w && text){
            lv_label_set_text(w, text);
            lv_obj_set_style_text_color(w, clr_of(color ? color[0] : 's'), 0);
        }
    } else if(!strcmp(verb, "OBJ")){
        char *type = strsep(&ln, "\t");
        char *id = strsep(&ln, "\t");
        if(!type || !id) return;
        char label[96] = {0}, value[96] = {0}, title[64] = {0}, btn[32] = {0}, btn2[32] = {0}, sub[32] = {0};
        char color = 'n';
        int sel = 0;
        char *kv;
        while(ln && (kv = strsep(&ln, "\t"))){
            char *eq = strchr(kv, '=');
            if(!eq) continue;
            *eq = 0;
            if(!strcmp(kv, "label")) strncpy(label, eq+1, sizeof(label)-1);
            else if(!strcmp(kv, "value")) strncpy(value, eq+1, sizeof(value)-1);
            else if(!strcmp(kv, "title")) strncpy(title, eq+1, sizeof(title)-1);
            else if(!strcmp(kv, "btn")) strncpy(btn, eq+1, sizeof(btn)-1);
            else if(!strcmp(kv, "btn2")) strncpy(btn2, eq+1, sizeof(btn2)-1);
            else if(!strcmp(kv, "sub")) strncpy(sub, eq+1, sizeof(sub)-1);
            else if(!strcmp(kv, "color")) color = eq[1];
            else if(!strcmp(kv, "sel")) sel = atoi(eq+1);
        }
        if(!strcmp(type, "list")){
            plist_t *L = NULL;
            for(int i = 0; i < p->n_lists; i++)
                if(!strcmp(p->lists[i].id, id)){ L = &p->lists[i]; break; }
            if(!L && p->n_lists < MAX_LISTS) L = &p->lists[p->n_lists++];
            if(L){
                strncpy(L->id, id, sizeof(L->id)-1);
                strncpy(L->title, title, sizeof(L->title)-1);
                strncpy(L->btn, btn, sizeof(L->btn)-1);
                strncpy(L->btn2, btn2, sizeof(L->btn2)-1);
                L->n_items = 0;
            }
        } else if(!strcmp(type, "item")){
            char lid[64], iid[64];
            if(split_lid(id, lid, sizeof lid, iid, sizeof iid) == 0){
                for(int i = 0; i < p->n_lists; i++){
                    plist_t *L = &p->lists[i];
                    if(strcmp(L->id, lid) || L->n_items >= MAX_ITEMS) continue;
                    pitem_t *it = &L->items[L->n_items++];
                    memset(it, 0, sizeof *it);
                    strncpy(it->iid, iid, sizeof(it->iid)-1);
                    strncpy(it->label, label, sizeof(it->label)-1);
                    strncpy(it->sub, sub, sizeof(it->sub)-1);
                    it->color = color;
                    it->sel = sel;
                    break;   /* 渲染统一由 LEND 触发, 避免逐项 O(n^2) 重绘 */
                }
            }
        } else {
            /* 修复: detail 关闭后(detail_open=0)不再接受 detail 内的 OBJ, 否则会把 widget 挂到已销毁的 d_zsw/d_zinfo/d_zmenu 触发 SIGSEGV */
            if(!p->detail_open) return;
            char t = (type[0] == 's') ? 's' : (type[0] == 'i') ? 'i' : 'm';
            detail_add(p, t, id, label, value, type[0] == 'i' ? color : 's');
        }
    } else if(!strcmp(verb, "LEND")){
        if(ln){
            for(int i = 0; i < p->n_lists; i++)
                if(!strcmp(p->lists[i].id, ln) && p->lists[i].open){
                    list_render(p, i);
                    break;
                }
        }
    } else if(!strcmp(verb, "SET")){
        char *id = strsep(&ln, "\t");
        if(!id) return;
        char value[96] = {0};
        char color = 0;
        int checked = -1;
        char *kv;
        while(ln && (kv = strsep(&ln, "\t"))){
            char *eq = strchr(kv, '=');
            if(!eq) continue;
            *eq = 0;
            if(!strcmp(kv, "value")) strncpy(value, eq+1, sizeof(value)-1);
            else if(!strcmp(kv, "color")) color = eq[1];
            else if(!strcmp(kv, "checked")) checked = atoi(eq+1);
        }
        if(strchr(id, '/')){
            char lid[64], iid[64];
            if(split_lid(id, lid, sizeof lid, iid, sizeof iid) == 0){
                for(int i = 0; i < p->n_lists; i++){
                    plist_t *L = &p->lists[i];
                    if(strcmp(L->id, lid)) continue;
                    for(int j = 0; j < L->n_items; j++){
                        if(strcmp(L->items[j].iid, iid)) continue;
                        if(value[0]) strncpy(L->items[j].sub, value, sizeof(L->items[j].sub)-1);
                        if(color) L->items[j].color = color;
                        if(checked >= 0) L->items[j].sel = checked;
                    }
                    if(L->open) list_render(p, i);
                }
            }
        } else {
            pobj_t *o = detail_find(p, id);
            if(o){
                if(o->type == 's' && checked >= 0){
                    if(checked) lv_obj_add_state(o->sw, LV_STATE_CHECKED);
                    else lv_obj_clear_state(o->sw, LV_STATE_CHECKED);
                } else if(o->val && value[0]){
                    lv_label_set_text(o->val, value);
                    lv_obj_set_style_text_color(o->val, clr_of(color ? color : 's'), 0);
                }
            }
        }
    }
}

/* ================= session ================= */

void ui_run(void){
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    lv_init();
    fonts_init();
    disp_init();
    indev_init();
    conf_load_theme();
    theme_load();
    styles_init();
    registry_scan();
    for(int i = 0; i < N_PL; i++)
        if(PL[i].visible && ipc_spawn(&PL[i]) == 0)
            ipc_send(&PL[i], "INIT");
    fprintf(stderr,"mudi-core: session start, plugins=%d\n", N_PL);
    fflush(stderr);

    build_home();

    /* session 开始立即开背光 */
    int bf0 = open("/sys/class/backlight/soc:backlight/bl_power", O_WRONLY);
    if(bf0 >= 0){ write(bf0, "0", 1); close(bf0); }

    int pk = open("/dev/input/event1", O_RDONLY|O_NONBLOCK);
    int ms = 0, bl_feed = 0;
    uint32_t wd_acc = 0;   /* 看门狗: 累计卡住时间 (ms), 单次慢渲染不误杀 */
    while(!g_quit){
        uint32_t wd0 = mudi_tick_ms();
        lv_timer_handler();
        lv_timer_handler();
        /* 延迟导航: 事件回调中不能调 nav_pop/lv_obj_del,否则 LVGL 事件链返回后
           继续访问已删除的 widget 树导致 SIGSEGV */
        if(g_defer_pop){ g_defer_pop = 0; nav_pop(); }
        if(g_defer_quit){ g_defer_quit = 0; g_quit = 1; continue; }
        for(int i = 0; i < N_PL; i++)
            if(PL[i].alive) ipc_readlines(&PL[i], proto_line);
        if(pk >= 0){
            struct input_event ev;
            while(read(pk, &ev, sizeof ev) > 0)
                if(ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1)
                    g_quit = 1;
        }
        usleep(5000);
        ms += 5; bl_feed += 5;
        if(bl_feed >= 15000){   /* 每15秒喂一次背光 */
            bl_feed = 0;
            int bf = open("/sys/class/backlight/soc:backlight/bl_power", O_WRONLY);
            if(bf >= 0){ write(bf, "0", 1); close(bf); }
        }
        if(ms >= 1000){
            ms = 0;
            sb_tick();
            for(int i = 0; i < N_PL; i++)
                if(PL[i].alive) ipc_send(&PL[i], "TICK");
            if(busy && ++busy_t >= 40) busy_show(0);
        }
        /* 看门狗: freetype 渲染中文字体可能单次 >3s, 不能按单次判死;
           累计卡住 >15s (持续死循环) 才强制退出会话 */
        uint32_t wd_dt = mudi_tick_ms() - wd0;
        if(wd_dt > 2000){
            wd_acc += wd_dt;
        } else if(wd_acc){
            wd_acc = 0;   /* 恢复, 清零累计 */
        }
        if(wd_acc > 15000){
            fprintf(stderr, "mudi-core: watchdog stuck acc=%u ms, quit\n",
                    (unsigned)wd_acc);
            fflush(stderr);
            g_quit = 1;
        }
    }
    ipc_stop_all();
    if(pk >= 0) close(pk);
    close(g_tfd);
    disp_done();
}
