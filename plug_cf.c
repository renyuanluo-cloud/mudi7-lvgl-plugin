/*
 * plug_cf.c — Cloudflare Tunnel 插件 (协议 v2)
 * 状态: 纯文件读 UCI + /proc (非 popen, 零阻塞)
 * 开关: uci set + commit + init.d reload (同 web 后台)
 */
#include "libplugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

static int ON = 0, SVC_BUSY = 0, SVC_T = 0, DETAIL_OPEN = 0;
static int CF_PID = 0;

/* ---- 二进制路径 (which 自动发现, 兜底官方包固定 /usr/bin/cloudflared) ---- */
static const char *cf_bin_path(void){
    static char bp[160] = "";
    static int done = 0;
    if(!done){
        done = 1;
        FILE *f = popen("which cloudflared 2>/dev/null", "r");
        if(f){
            if(fgets(bp, sizeof bp, f)){}
            pclose(f);
            char *nl = strchr(bp, '\n'); if(nl) *nl = 0;
            char *sp = strchr(bp, ' ');  if(sp) *sp = 0;
        }
        if(!bp[0]) snprintf(bp, sizeof bp, "/usr/bin/cloudflared");
    }
    return bp;
}
static int cf_match_bin(const char *nm){
    return strstr(nm, cf_bin_path()) != NULL;
}

/* UCI 检测: 官方 cloudflared 包自带 /etc/config/cloudflared (enabled/token) */
static int HAS_UCI = 0;
static void cf_check_uci(void){ HAS_UCI = access("/etc/config/cloudflared", F_OK) == 0; }

/* UCI 配置文件里读 enabled */
static int uci_enabled(void){
    FILE *f = fopen("/etc/config/cloudflared", "r");
    if(!f) return 0;
    char ln[256]; int r = 0;
    while(fgets(ln, sizeof ln, f)){
        char *p = strstr(ln, "enabled");
        if(p && (p = strchr(p, '\''))) r = (p[1] == '1');
    }
    fclose(f);
    return r;
}

/* /proc 扫 cloudflared 进程 (匹配二进制路径, 排除插件自身) */
static int cf_running(void){
    CF_PID = 0;
    DIR *d = opendir("/proc");
    if(!d) return 0;
    struct dirent *de;
    while((de = readdir(d))){
        if(de->d_name[0] < '0' || de->d_name[0] > '9') continue;
        char p[64]; snprintf(p, sizeof p, "/proc/%s/cmdline", de->d_name);
        FILE *f = fopen(p, "r");
        if(!f) continue;
        char nm[128] = {0}; size_t r = fread(nm, 1, sizeof nm - 1, f); fclose(f);
        nm[r] = 0;
        if(cf_match_bin(nm)){
            CF_PID = atoi(de->d_name);
            closedir(d);
            return 1;
        }
    }
    closedir(d);
    return 0;
}

/* /proc/PID/fd 统计 socket */
static int cf_conns(void){
    if(CF_PID <= 0) return 0;
    char p[64]; snprintf(p, sizeof p, "/proc/%d/fd", CF_PID);
    DIR *d = opendir(p);
    if(!d) return 0;
    int n = 0; struct dirent *de;
    while((de = readdir(d))){
        if(de->d_name[0] == '.') continue;
        char lk[128]; snprintf(lk, sizeof lk, "%s/%s", p, de->d_name);
        char tgt[64]; int r = readlink(lk, tgt, sizeof tgt - 1);
        if(r > 0){ tgt[r] = 0; if(strstr(tgt, "socket")) n++; }
    }
    closedir(d);
    return n / 2;
}

/* ---------- push ---------- */

static void push_card(void){
    mudi_card(ON);
    if(ON){
        int n = cf_conns();
        char b[32]; snprintf(b, sizeof b, "%d 连接", n);
        mudi_cf("r2", "已连接", 'g');
        mudi_cf("r3l", b, 's');
        mudi_cf("r3r", "运行中", 'g');
    } else {
        mudi_cf("r2", "已停止", 'd');
        mudi_cf("r3l", "点按进入详情 >", 'f');
        mudi_cf("r3r", "", 'd');
    }
    mudi_cf("r4", "", 'd');
}

static void push_detail(void){
    if(!DETAIL_OPEN) return;
    mudi_set("status", ON ? "运行中" : "已停止", ON ? 'g' : 'd');
    mudi_set("dep", HAS_UCI ? "UCI 配置已就绪" : "缺 cloudflared 配置", HAS_UCI ? 'd' : 'o');
    if(!ON){ mudi_set("conn", "--", 's'); return; }
    char b[64]; int n = cf_conns();
    snprintf(b, sizeof b, "%d 连接", n);
    mudi_set("conn", b, 's');
}

/* ---------- lifecycle ---------- */

static void on_init(void){
    cf_check_uci();
    ON = uci_enabled() && cf_running();
    push_card();
}

static void on_open(void){
    DETAIL_OPEN = 1;
    cf_check_uci();
    ON = uci_enabled() && cf_running();
    mudi_switch("svc", "Cloudflare Tunnel", ON);
    mudi_info("status", "状态", ON ? "运行中" : "已停止", ON ? 'g' : 'd');
    mudi_info("dep", "前置", HAS_UCI ? "UCI 配置已就绪" : "缺 cloudflared 配置", HAS_UCI ? 'd' : 'o');
    mudi_info("conn", "连接数", "--", 's');
    push_card();
    push_detail();
}

static void on_close(void){ DETAIL_OPEN = 0; }

static void on_tick(int sec){
    if(SVC_BUSY){
        SVC_T++;
        cf_running();
        if(ON){ if(CF_PID > 0 || SVC_T > 20){ SVC_BUSY = 0; ON = 1; } }
        else  { if(CF_PID == 0 || SVC_T > 10){ SVC_BUSY = 0; ON = 0; } }
        if(!SVC_BUSY){
            mudi_checked("svc", ON);
            push_card(); push_detail();
            mudi_busy(0);
        }
        return;
    }
    if(sec % 3 == 0){
        int prev = ON;
        ON = uci_enabled() && cf_running();
        if(ON != prev) mudi_checked("svc", ON);
        push_card();
    }
    if(DETAIL_OPEN) push_detail();
}

static void on_ev(const char *id, const char *arg){
    if(!strcmp(id, "svc")){
        if(SVC_BUSY) return;
        SVC_BUSY = 1; SVC_T = 0;
        ON = (arg[0] == '1');
        mudi_busy(1);
        if(ON) system("uci set cloudflared.config.enabled=1 && uci commit cloudflared && /etc/init.d/cloudflared reload >/dev/null 2>&1 &");
        else   system("uci set cloudflared.config.enabled=0 && uci commit cloudflared && /etc/init.d/cloudflared stop >/dev/null 2>&1 &");
    }
}

static mudi_plugin_t P = {
    .name     = "Cloudflare Tunnel",
    .on_init  = on_init,
    .on_open  = on_open,
    .on_close = on_close,
    .on_tick  = on_tick,
    .on_ev    = on_ev,
};

int main(void){ mudi_run(&P); return 0; }
