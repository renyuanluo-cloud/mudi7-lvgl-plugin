/* touch_drv.c — LVGL evdev touch input driver */
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <dirent.h>
#include <string.h>

static int touch_fd = -1;

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    if (touch_fd < 0) { data->state = LV_INDEV_STATE_REL; return; }

    struct input_event ev;
    static int last_x = 0, last_y = 0, pressed = 0;

    /* drain all pending events, keep last state */
    while (read(touch_fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X) last_x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y) last_y = ev.value;
            else if (ev.code == ABS_MT_TRACKING_ID) {
                pressed = (ev.value >= 0);
            }
        }
        else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            pressed = ev.value;
        }
    }

    if (pressed) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = last_x;
        data->point.y = last_y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

int touchdrv_init(void)
{
    /* find touch device */
    DIR *d = opendir("/dev/input");
    if (!d) goto fallback;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[64]; snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long bits[8] = {0};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits);
        if (bits[0] & (1UL << ABS_MT_POSITION_X)) {
            close(fd);
            touch_fd = open(path, O_RDONLY | O_NONBLOCK);
            closedir(d);
            printf("[touch] %s\n", path);
            goto register_indev;
        }
        close(fd);
    }
    closedir(d);
fallback:
    touch_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (touch_fd < 0) { printf("[touch] no device\n"); return -1; }

register_indev:
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);
    return 0;
}

void touchdrv_deinit(void)
{
    if (touch_fd >= 0) close(touch_fd);
}
