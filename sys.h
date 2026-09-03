#ifndef MUDI_SYS_H
#define MUDI_SYS_H

#include "lvgl/lvgl.h"

int  sys_cpu(void);                          /* usage % */
int  sys_ram(void);                          /* usage % */
int  sys_batt(void);                         /* 0-100, -1 if n/a */
void sys_net(unsigned *up, unsigned *down);  /* bytes/s */

lv_color_t clr_delay(int ms);   /* -1 -> gray */
lv_color_t clr_pct(int pct);    /* cpu/ram levels */
lv_color_t clr_batt(int pct);

#endif
