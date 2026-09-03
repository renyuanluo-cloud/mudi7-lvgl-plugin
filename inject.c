/* inject.c — evdev 事件注入器 (tap/swipe/power) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <linux/input.h>

#define DEV "/dev/input/event0"

static void emit(int fd, int type, int code, int val){
    struct input_event ev;
    struct timeval tv = {0,0};
    gettimeofday(&tv, NULL);
    ev.input_event_sec = tv.tv_sec;
    ev.input_event_usec = tv.tv_usec;
    ev.type = type;
    ev.code = code;
    ev.value = val;
    write(fd, &ev, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(fd, &ev, sizeof(ev));
}

static void tap(int fd, int x, int y){
    emit(fd, EV_ABS, ABS_MT_TRACKING_ID, 1);
    emit(fd, EV_ABS, ABS_MT_POSITION_X, x);
    emit(fd, EV_ABS, ABS_MT_POSITION_Y, y);
    emit(fd, EV_KEY, BTN_TOUCH, 1);
    usleep(20000);
    emit(fd, EV_KEY, BTN_TOUCH, 0);
    emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
}

static void swipe(int fd){
    for(int x = 0; x <= 200; x += 10){
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, 1);
        emit(fd, EV_ABS, ABS_MT_POSITION_X, x);
        emit(fd, EV_ABS, ABS_MT_POSITION_Y, 160);
        emit(fd, EV_KEY, BTN_TOUCH, 1);
        usleep(5000);
    }
    emit(fd, EV_KEY, BTN_TOUCH, 0);
    emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
}

static void key_power(int fd){
    emit(fd, EV_KEY, KEY_POWER, 1);
    usleep(100000);
    emit(fd, EV_KEY, KEY_POWER, 0);
}

int main(int argc, char **argv){
    if(argc < 2) return 1;
    int fd = open(DEV, O_WRONLY);
    if(fd < 0) return 1;
    if(!strcmp(argv[1], "tap") && argc >= 4)
        tap(fd, atoi(argv[2]), atoi(argv[3]));
    else if(!strcmp(argv[1], "swipe"))
        swipe(fd);
    else if(!strcmp(argv[1], "power"))
        key_power(fd);
    close(fd);
    return 0;
}
