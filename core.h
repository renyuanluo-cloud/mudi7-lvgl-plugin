/*
 * core.h — Mudi Dashboard Core 内部共享结构
 * 仅 core 内部使用 (ipc.c / core_ui.c)，插件永远看不到这里。
 */
#ifndef MUDI_CORE_H
#define MUDI_CORE_H

#include "lvgl/lvgl.h"
#include <sys/types.h>

#define MAX_PLUGINS 8
#define MAX_LISTS   4
#define MAX_ITEMS   64

typedef struct {
    char id[32], name[64], exec[192];
    char version[16], api[8], permissions[96];
} manifest_t;

/* detail 页对象 (switch/info/menu) */
typedef struct pobj {
    char id[64];
    char type;                 /* 's' switch, 'i' info, 'm' menu */
    lv_obj_t *row, *val, *sw;
    struct pobj *next;
} pobj_t;

typedef struct {
    char iid[64], label[96], sub[32];
    char color;
    int  sel;
} pitem_t;

typedef struct {
    char id[64], title[64], btn[32], btn2[32];
    pitem_t items[MAX_ITEMS];
    int  n_items;
    int  open;                 /* 列表页当前是否打开 */
    lv_obj_t *cont;            /* 打开时的容器 (重绘用) */
    lv_obj_t *page;            /* 复用的列表页(隐藏时不删, 再打开时复用) */
} plist_t;

typedef struct plugin {
    manifest_t m;
    int  visible;
    pid_t pid;
    int  fd_r, fd_w, alive;
    char lbuf[2048];
    int  llen;
    /* 入口卡片 */
    lv_obj_t *card, *badge, *badge_t, *r2, *r3l, *r3r, *r4;
    int  card_on;
    /* 详情页 */
    lv_obj_t *detail_page;      /* 详情页对象 (复用: pop 只隐藏, 下次 open 复用同一页面) */
    pobj_t *objs;
    int  detail_open;           /* 详情页当前是否打开(用于关闭后丢弃 plugin 端残留的 OBJ 消息) */
    plist_t lists[MAX_LISTS];
    int  n_lists;
} plugin_t;

/* ipc.c */
extern plugin_t PL[MAX_PLUGINS];
extern int N_PL;
int  ipc_spawn(plugin_t *p);
void ipc_send(plugin_t *p, const char *fmt, ...);
int  ipc_readlines(plugin_t *p, void (*cb)(plugin_t *, char *));  /* 返回处理行数, EOF 时标记 alive=0 */
void ipc_stop_all(void);

/* registry.c */
int  registry_scan(void);                 /* 填充 PL[].m / PL[].visible, 返回插件数 */
void registry_save_visible(void);

#endif
