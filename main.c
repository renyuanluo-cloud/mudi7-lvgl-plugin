#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/input.h>

#include "ui.h"

static volatile sig_atomic_t g_term = 0;
static int g_lf = -1;

static void sigterm_cb(int sig){ g_term = 1; }

/* 从 /proc/X/comm 查找 gl_screen PID (替代 pgrep) */
static int gl_pid(void){
    DIR *d = opendir("/proc");
    if(!d) return 0;
    struct dirent *de;
    int pid = 0;
    while((de = readdir(d)) && !pid){
        int p = atoi(de->d_name);
        if(p <= 0) continue;
        char buf[32] = {0}, path[64];
        snprintf(path, sizeof path, "/proc/%d/comm", p);
        FILE *f = fopen(path, "r");
        if(!f) continue;
        if(fgets(buf, sizeof buf, f) && strstr(buf, "gl_screen")) pid = p;
        fclose(f);
    }
    closedir(d);
    return pid;
}

/* wait for left-edge right-swipe on the touchscreen (lock screen visible) */
static int wait_swipe(void){
    int tfd = -1;
    for(int i = 0; i < 6; i++){
        char p[64];
        snprintf(p,sizeof p,"/dev/input/event%d",i);
        tfd = open(p, O_RDONLY);
        if(tfd < 0) continue;
        struct input_absinfo a;
        if(ioctl(tfd, EVIOCGABS(ABS_MT_POSITION_X), &a) >= 0) break;
        close(tfd); tfd = -1;
    }
    if(tfd < 0) return -1;
    /* 排空触摸残留: 锁屏/会话切换瞬间的触摸事件可能被误判为左滑手势,
       导致误进会话冻结 gl_screen (用户按电源唤醒时表现为无法唤醒) */
    {
        int df = open("/dev/input/event0", O_RDONLY|O_NONBLOCK);
        if(df >= 0){ struct input_event ev; while(read(df, &ev, sizeof ev) > 0){} close(df); }
        usleep(50000);
        df = open("/dev/input/event0", O_RDONLY|O_NONBLOCK);
        if(df >= 0){ struct input_event ev; while(read(df, &ev, sizeof ev) > 0){} close(df); }
    }
    int slot = 0, act[10] = {0}, sx[10] = {0}, sy[10] = {0};
    struct timespec ts[10];
    while(1){
        if(g_term){ close(tfd); return -1; }
        struct input_event ev;
        if(read(tfd,&ev,sizeof ev) <= 0){ usleep(5000); continue; }
        if(ev.type != EV_ABS) continue;
        if(ev.code == ABS_MT_SLOT) slot = ev.value;
        if(ev.code == ABS_MT_TRACKING_ID){
            if(ev.value >= 0){ act[slot] = 1; sx[slot] = 0; sy[slot] = 0; clock_gettime(CLOCK_MONOTONIC,&ts[slot]); }
            else act[slot] = 0;
        }
        if(ev.code == ABS_MT_POSITION_X && act[slot] && sx[slot] == 0) sx[slot] = ev.value;
        if(ev.code == ABS_MT_POSITION_Y && act[slot] && sy[slot] == 0) sy[slot] = ev.value;
        if(act[slot] && sx[slot] > 0){
            int dx = (ev.code == ABS_MT_POSITION_X) ? ev.value - sx[slot] : 0;
            int dy = (ev.code == ABS_MT_POSITION_Y) ? ev.value - sy[slot] : 0;
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC,&now);
            long ms = (now.tv_sec-ts[slot].tv_sec)*1000 + (now.tv_nsec-ts[slot].tv_nsec)/1000000;
            /* 触发判定收紧: 起点贴左边缘(<20px) + 水平右滑主导(|dx| > 2|dy|),
               避免与 gl_screen 上滑解锁(x 任意)冲突误判 */
            if(sx[slot] < 20 && dx > 50 && abs(dx) > 2*abs(dy) && ms < 800){
                close(tfd); return 0;
            }
        }
    }
}

static void cleanup(int glpid){
    if(glpid > 0) kill(glpid, SIGCONT);
    if(g_lf >= 0){
        unlink("/tmp/mudi-core.lock");
        close(g_lf);
        g_lf = -1;
    }
    /* 排空触摸事件 */
    int df = open("/dev/input/event0", O_RDONLY|O_NONBLOCK);
    if(df >= 0){ struct input_event ev; while(read(df, &ev, sizeof ev) > 0){} close(df); }
    usleep(2000000);
}

int main(void){
    g_lf = open("/tmp/mudi-core.lock", O_CREAT|O_RDWR, 0644);
    if(g_lf < 0 || lockf(g_lf, F_TLOCK, 0) < 0){
        fprintf(stderr,"mudi-core: already running\n");
        return 1;
    }
    /* 注意: 不能设 SIGCHLD=SIG_IGN! 否则 waitpid(135行) 立即返回 ECHILD,
       父进程不等会话结束就继续 wait_swipe, 用户在外屏上的滑动会被误判为
       新会话手势 → fork 出多个会话/插件进程堆积。
       子进程由 waitpid 正常回收 (插件孙进程僵尸由 init 收养, 无累积)。 */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, sigterm_cb);

    while(!g_term){
        if(wait_swipe() < 0) break;
        int pid = gl_pid();
        /* 如果 gl_screen 还停在 T 状态, 先恢复 */
        if(pid > 0){
            char sp[64]; snprintf(sp, sizeof sp, "/proc/%d/status", pid);
            FILE *sf = fopen(sp, "r");
            if(sf){
                char ln[128]; int is_t = 0;
                while(fgets(ln, sizeof ln, sf))
                    if(strstr(ln, "State:") && strstr(ln, "T (stopped)")){ is_t = 1; break; }
                fclose(sf);
                if(is_t){
                    kill(pid, SIGCONT); usleep(500000);
                    fprintf(stderr,"mudi-core: recovered gl_screen %d from T\n", pid);
                }
            }
        }
        if(pid > 0) kill(pid, SIGSTOP);
        usleep(200000);
        pid_t c = fork();
        if(c == 0){
            { /* 记 session 开始 */
                FILE *log = fopen("/tmp/mudi-exit.log", "a");
                if(log){
                    time_t t = time(NULL);
                    fprintf(log, "%.24s session+ pid=%d\n", ctime(&t), getpid());
                    fclose(log);
                }
            }
            ui_run();
            fprintf(stderr,"mudi-core: session end\n");
            _exit(0);
        }
        /* 等子进程结束 */
        int st = 0;
        waitpid(c, &st, 0);
        { /* 记退出码到文件，拔电池后也能看 */
            FILE *log = fopen("/tmp/mudi-exit.log", "a");
            if(log){
                time_t t = time(NULL);
                fprintf(log, "%.24s pid=%d exit=%x signal=%d\n",
                        ctime(&t), c, st>>8, st&0x7f);
                fclose(log);
            }
        }
        fprintf(stderr,"mudi-core: child %d exit, status=%x\n", c, st);
        fflush(stderr);
        /* 父进程兜底清锁 */
        unlink("/tmp/mudi-core.lock");
        close(g_lf);
        g_lf = open("/tmp/mudi-core.lock", O_CREAT|O_RDWR, 0644);
        if(g_lf >= 0) lockf(g_lf, F_TLOCK, 0);
        if(pid > 0){
            kill(pid, SIGCONT);
            int df = open("/dev/input/event0", O_RDONLY|O_NONBLOCK);
            if(df >= 0){ struct input_event ev; while(read(df, &ev, sizeof ev) > 0){} close(df); }
            usleep(2000000);
        } else usleep(300000);
    }
    cleanup(gl_pid());
    return 0;
}
