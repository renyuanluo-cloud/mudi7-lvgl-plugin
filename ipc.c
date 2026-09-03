#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

plugin_t PL[MAX_PLUGINS];
int N_PL = 0;

int ipc_spawn(plugin_t *p){
    int in[2], out[2];   /* in: core->plugin, out: plugin->core */
    if(pipe(in) || pipe(out)) return -1;
    pid_t pid = fork();
    if(pid == 0){
        dup2(in[0], 0);
        dup2(out[1], 1);
        close(in[0]); close(in[1]);
        close(out[0]); close(out[1]);
        execl(p->m.exec, p->m.exec, (char*)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    p->pid = pid;
    p->fd_w = in[1];
    p->fd_r = out[0];
    p->alive = 1;
    p->llen = 0;
    int fl = fcntl(p->fd_r, F_GETFL);
    fcntl(p->fd_r, F_SETFL, fl | O_NONBLOCK);
    return 0;
}

void ipc_send(plugin_t *p, const char *fmt, ...){
    if(!p->alive) return;
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b - 1, fmt, ap);
    va_end(ap);
    if(n <= 0) return;
    b[n++] = '\n';
    if(write(p->fd_w, b, n) < 0) p->alive = 0;
}

int ipc_readlines(plugin_t *p, void (*cb)(plugin_t *, char *)){
    if(!p->alive) return 0;
    int done = 0;
    for(;;){
        char *nl = memchr(p->lbuf, '\n', p->llen);
        if(!nl) break;
        *nl = 0;
        cb(p, p->lbuf);
        int rest = p->llen - (int)(nl - p->lbuf) - 1;
        memmove(p->lbuf, nl + 1, rest);
        p->llen = rest;
        done++;
    }
    char tmp[1024];
    int n = read(p->fd_r, tmp, sizeof tmp);
    if(n == 0){
        p->alive = 0;   /* EOF: 插件退出 */
        fprintf(stderr,"mudi-core: plugin %s EOF\n", p->m.id);
        fflush(stderr);
        return done;
    }
    if(n < 0) return done;
    if(p->llen + n >= (int)sizeof(p->lbuf)) p->llen = 0;   /* 溢出防御: 丢弃 */
    memcpy(p->lbuf + p->llen, tmp, n);
    p->llen += n;
    for(;;){
        char *nl = memchr(p->lbuf, '\n', p->llen);
        if(!nl) break;
        *nl = 0;
        cb(p, p->lbuf);
        int rest = p->llen - (int)(nl - p->lbuf) - 1;
        memmove(p->lbuf, nl + 1, rest);
        p->llen = rest;
        done++;
    }
    return done;
}

void ipc_stop_all(void){
    for(int i = 0; i < N_PL; i++){
        if(PL[i].alive){
            ipc_send(&PL[i], "EXIT");
        }
    }
    usleep(100000);
    for(int i = 0; i < N_PL; i++){
        if(PL[i].alive){
            kill(PL[i].pid, SIGTERM);
            close(PL[i].fd_r);
            close(PL[i].fd_w);
            PL[i].alive = 0;
        }
    }
}
