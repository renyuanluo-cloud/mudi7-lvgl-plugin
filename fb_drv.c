/* fb_drv.c — LVGL framebuffer display driver for /dev/fb0 RGB565 */
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string.h>

static int fb_fd = -1;
static uint16_t *fb_map = NULL;
static size_t fb_size = 0;
static int fb_w = 240, fb_h = 320;

/* back buffer for double-buffering */
static uint16_t *back_buf = NULL;

static void fb_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map)
{
    if (!back_buf) { lv_disp_flush_ready(drv); return; }

    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    /* copy rendered pixels to back buffer */
    for (int y = 0; y < h; y++) {
        memcpy(&back_buf[(area->y1 + y) * fb_w + area->x1],
               &px_map[y * w], w * 2);
    }

    /* commit entire back buffer to real fb0 */
    if (fb_map)
        memcpy(fb_map, back_buf, fb_size);

    lv_disp_flush_ready(drv);
}

static void fb_rounder(lv_disp_drv_t *drv, lv_area_t *area)
{
    /* No rounding needed */
}

static void fb_set_px(lv_disp_drv_t *drv, uint8_t *buf, lv_coord_t buf_w,
                       lv_coord_t x, lv_coord_t y, lv_color_t color, lv_opa_t opa)
{
    uint16_t *dest = (uint16_t *)buf;
    dest[y * buf_w + x] = color.full;
}

int fbdrv_init(void)
{
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) { perror("fb0"); return -1; }

    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vi);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fi);

    fb_w = vi.xres;
    fb_h = vi.yres;
    fb_size = fi.smem_len;

    printf("[fb] %dx%d, %dbpp, %zu bytes\n", fb_w, fb_h, vi.bits_per_pixel, fb_size);

    fb_map = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_map == MAP_FAILED) { perror("mmap"); close(fb_fd); return -1; }

    /* allocate back buffer */
    back_buf = malloc(fb_size);
    if (!back_buf) { munmap(fb_map, fb_size); close(fb_fd); return -1; }
    /* initialize back buffer with current screen content */
    memcpy(back_buf, fb_map, fb_size);

    /* register display driver */
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = fb_w;
    drv.ver_res = fb_h;
    drv.flush_cb = fb_flush;
    drv.rounder_cb = fb_rounder;
    drv.set_px_cb = fb_set_px;
    drv.screen_transp = 0;
    drv.direct_mode = 0; /* use double buffering */

    /* draw buffers — LVGL renders into these */
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[fb_w * 20]; /* partial buffer for rendering bands */
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, fb_w * 20);
    drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&drv);
    return 0;
}

void fbdrv_deinit(void)
{
    if (back_buf) { free(back_buf); back_buf = NULL; }
    if (fb_map && fb_map != MAP_FAILED) { munmap(fb_map, fb_size); fb_map = NULL; }
    if (fb_fd >= 0) { close(fb_fd); fb_fd = -1; }
}

int fbdrv_width(void)  { return fb_w; }
int fbdrv_height(void) { return fb_h; }
