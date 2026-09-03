#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        0

#define LV_HOR_RES_MAX          240
#define LV_VER_RES_MAX          320

#define LV_DPI_DEF              143

#define LV_MEM_SIZE             (128 * 1024)
#define LV_MEM_CUSTOM           0

#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  "mudi_tick.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (mudi_tick_ms())
#define LV_TICK_CUSTOM_SYS_CLOCK_MONOTONIC

#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0
#define LV_USE_LOG              0

#define LV_USE_ASSERT_NULL      0
#define LV_USE_ASSERT_MALLOC    0

#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_USE_FREETYPE         1

#define LV_USE_FS_IF            0
#define LV_USE_GPU              0

#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_BASIC      1
#define LV_USE_THEME_MONO       0

#define LV_USE_ANIMATION        1
#define LV_USE_SHADOW           0
#define LV_USE_GRADIENT         0
#define LV_USE_BLEND_MODES      0
#define LV_USE_OPA_SCALE        1
#define LV_USE_OUTLINE          0
#define LV_USE_PATTERN          0

/* widgets */
#define LV_USE_ARC              1
#define LV_USE_BAR              0
#define LV_USE_BTN              1
#define LV_USE_BTNMATRIX        0
#define LV_USE_CALENDAR         0
#define LV_USE_CANVAS           0
#define LV_USE_CHECKBOX         0
#define LV_USE_DROPDOWN         0
#define LV_USE_IMG              0
#define LV_USE_LABEL            1
#define LV_USE_LINE             0
#define LV_USE_LIST             0
#define LV_USE_MENU             0
#define LV_USE_METER            0
#define LV_USE_MSGBOX           0
#define LV_USE_ROLLER           0
#define LV_USE_SLIDER           0
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          1
#define LV_USE_SWITCH           1
#define LV_USE_TABLE            0
#define LV_USE_TABVIEW          0
#define LV_USE_TEXTAREA         0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

#define LV_USE_CONT             1
#define LV_USE_OBJ              1

#endif
