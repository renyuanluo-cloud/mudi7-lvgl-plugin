#include "libplugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

/* ================= 插件配置 ================= */

/* 定位 conf 文件: /etc/mudi/plugins/<可执行文件名>.conf */
static const char *conf_path(void){
    static char path[160] = "";
    if(path[0]) return path;
    char exe[128];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    const char *base = "plugin";
    if(n > 0){
        exe[n] = 0;
        const char *s = strrchr(exe, '/');
        base = s ? s + 1 : exe;
    }
    snprintf(path, sizeof path, "/etc/mudi/plugins/%s.conf", base);
    return path;
}

static void trim(char *s){
    /* 去行首空白 */
    char *p = s;
    while(*p == ' ' || *p == '\t') p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    /* 去行尾空白 (\r\n 空格 tab) */
    size_t l = strlen(s);
    while(l && (s[l-1] == ' ' || s[l-1] == '\t' || s[l-1] == '\r' || s[l-1] == '\n'))
        s[--l] = 0;
}

int mudi_conf_get(const char *key, const char *dflt, char *out, int sz){
    if(!out || sz <= 0) return 0;
    if(dflt) { strncpy(out, dflt, sz - 1); out[sz-1] = 0; }
    else out[0] = 0;
    FILE *f = fopen(conf_path(), "r");
    if(!f) return 0;
    char ln[512];
    int found = 0;
    while(fgets(ln, sizeof ln, f)){
        char *nl = strchr(ln, '\n');
        if(nl) *nl = 0;
        char *e = ln;
        while(*e == ' ' || *e == '\t') e++;
        if(!*e || *e == '#') continue;          /* 空行/注释 */
        char *eq = strchr(e, '=');
        if(!eq) continue;
        char k[96];
        int kl = (int)(eq - e);
        if(kl <= 0 || kl >= (int)sizeof k) continue;
        memcpy(k, e, kl); k[kl] = 0;
        trim(k);
        if(strcmp(k, key)) continue;            /* 键不匹配 */
        char *v = eq + 1;
        trim(v);                                /* 去值首尾空白 */
        strncpy(out, v, sz - 1); out[sz-1] = 0;
        found = 1;
        break;
    }
    fclose(f);
    return found;
}

void mudi_card(int on){ printf("CARD\t%s\n", on ? "ON" : "OFF"); fflush(stdout); }

void mudi_cf(const char *row, const char *text, char color){
    printf("CF\t%s\t%s\t%c\n", row, text, color);
    fflush(stdout);
}

void mudi_switch(const char *id, const char *label, int on){
    printf("OBJ\tswitch\t%s\tlabel=%s\tvalue=%d\n", id, label, on ? 1 : 0);
    fflush(stdout);
}

void mudi_info(const char *id, const char *label, const char *value, char color){
    printf("OBJ\tinfo\t%s\tlabel=%s\tvalue=%s\tcolor=%c\n", id, label, value, color);
    fflush(stdout);
}

void mudi_menu(const char *id, const char *label, const char *value){
    printf("OBJ\tmenu\t%s\tlabel=%s\tvalue=%s\n", id, label, value);
    fflush(stdout);
}

void mudi_list(const char *id, const char *title, const char *btn, const char *btn2){
    printf("OBJ\tlist\t%s\ttitle=%s\tbtn=%s\tbtn2=%s\n", id, title, btn ? btn : "", btn2 ? btn2 : "");
    fflush(stdout);
}

void mudi_item(const char *list, const char *iid, const char *label,
               const char *sub, char color, int sel){
    printf("OBJ\titem\t%s/%s\tlabel=%s\tsub=%s\tcolor=%c\tsel=%d\n",
           list, iid, label, sub, color, sel);
    fflush(stdout);
}

void mudi_lend(const char *list){
    printf("LEND\t%s\n", list);
    fflush(stdout);
}

void mudi_set(const char *id, const char *value, char color){
    printf("SET\t%s\tvalue=%s\tcolor=%c\n", id, value, color);
    fflush(stdout);
}

void mudi_checked(const char *id, int on){
    printf("SET\t%s\tchecked=%d\n", id, on ? 1 : 0);
    fflush(stdout);
}

void mudi_busy(int on){
    printf("BUSY\t%d\n", on ? 1 : 0);
    fflush(stdout);
}

int mudi_run(mudi_plugin_t *p){
    signal(SIGCHLD, SIG_IGN);  /* H1: 防 fork 僵尸堆积 */
    signal(SIGPIPE, SIG_IGN);
    char line[2048];
    int tick = 0;

    printf("HELLO\t%d\t%s\n", MUDI_API_VER, p->name);
    fflush(stdout);

    while(fgets(line, sizeof line, stdin)){
        char *nl = strchr(line, '\n');
        if(nl) *nl = 0;
        if(!line[0]) continue;

        if(!strcmp(line, "EXIT")) break;
        else if(!strcmp(line, "TICK")){ if(p->on_tick) p->on_tick(tick++); }
        else if(!strcmp(line, "OPEN")){ if(p->on_open) p->on_open(); }
        else if(!strcmp(line, "CLOSE")){ if(p->on_close) p->on_close(); }
        else if(!strcmp(line, "INIT")){ if(p->on_init) p->on_init(); }
        else if(!strncmp(line, "EV\t", 3)){
            if(p->on_ev){
                char *id = line + 3;
                char *arg = strchr(id, '\t');
                if(arg){ *arg = 0; p->on_ev(id, arg + 1); }
                else p->on_ev(id, "");
            }
        }
    }
    return 0;
}
