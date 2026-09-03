/*
 * plug_zt.c — ZeroTier 插件 (协议 v2)
 * 数据源: 本地 /var/lib/zerotier-luci/ (networks.d/*.conf + identity.secret + zerotier-cli -D)
 */
#include "libplugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

static char ZT_DIR[128] = "";
static char ZT_IP[32] = "";

static int ON = 0;
static int ZT_RUN = 0;
static int DETAIL_OPEN = 0;
static char ZT_ID[20] = "";

/* 链路状态监控 (异步) — 读 zerotier-cli peers 全部行, 取整体链路最优值 (DIRECT/RELAY),
   不区分具体 peer 角色, 语义 = 当前节点间是直连还是中继 */
static int  LINK_LAT = -1;         /* -1 未知/不可用 */
static char LINK_TYPE[16] = "";    /* DIRECT / RELAY */
static int  LINK_BUSY = 0;
#define LINK_OUT  "/tmp/mudi_zt_peers.txt"
#define LINK_DONE "/tmp/mudi_zt_peers.done"

/* ================= 本地数据获取 ================= */

/* 在实际的 ZT 数据目录 */
static void zt_find_dir(void){
    if(ZT_DIR[0]) return;
    /* 优先检查 /var/lib/zerotier-one (symlink to actual dir) */
    if(!access("/var/lib/zerotier-one/identity.secret", F_OK)){
        snprintf(ZT_DIR, sizeof ZT_DIR, "/var/lib/zerotier-one");
        return;
    }
    /* 检查 _sample_config 等后缀 */
    DIR *d = opendir("/var/lib");
    if(!d) return;
    struct dirent *de;
    while((de = readdir(d))){
        if(!strncmp(de->d_name, "zerotier-one_", 13)){
            char p[256];
            snprintf(p, sizeof p, "/var/lib/%s/identity.secret", de->d_name);
            if(!access(p, F_OK)){
                snprintf(ZT_DIR, sizeof ZT_DIR, "/var/lib/%s", de->d_name);
                break;
            }
        }
    }
    closedir(d);
}

static int zt_running(void){
    zt_find_dir();
    if(!ZT_DIR[0]) return 0;
    char p[256];
    snprintf(p, sizeof p, "%s/zerotier-one.port", ZT_DIR);
    return !access(p, F_OK);
}

static void zt_get_id(char *out, int sz){
    out[0] = 0;
    zt_find_dir();
    if(!ZT_DIR[0]) return;
    char p[256];
    snprintf(p, sizeof p, "%s/identity.secret", ZT_DIR);
    FILE *f = fopen(p, "r");
    if(!f) return;
    char buf[256] = {0};
    if(fgets(buf, sizeof buf, f)){
        char *colon = strchr(buf, ':');
        if(colon){
            int l = (colon - buf) < sz - 1 ? (colon - buf) : sz - 1;
            memcpy(out, buf, l);
            out[l] = 0;
        }
    }
    fclose(f);
}

/* 从 ip a show 获取 ZT IP */
static void zt_get_ip(void){
    ZT_IP[0] = 0;
    FILE *f = popen("ip a show 2>/dev/null | grep -A2 zt | grep 'inet ' | awk '{print $2}' | cut -d/ -f1 | head -1", "r");
    if(!f) return;
    char b[32];
    if(fgets(b, sizeof b, f)){
        char *nl = strchr(b, '\n'); if(nl) *nl = 0;
        if(b[0]) snprintf(ZT_IP, sizeof ZT_IP, "%s", b);
    }
    pclose(f);
}

/* 版本: zerotier-cli info 输出 "200 info <ver> ONLINE ...", 取第 3 字段 (缓存) */
static char ZT_VER[16] = "";
static void zt_get_ver(void){
    if(ZT_VER[0]) return;
    zt_find_dir();
    char cmd[256], b[256] = {0};
    if(ZT_DIR[0])
        snprintf(cmd, sizeof cmd, "zerotier-cli -D%s info 2>/dev/null", ZT_DIR);
    else
        snprintf(cmd, sizeof cmd, "zerotier-cli info 2>/dev/null");
    FILE *f = popen(cmd, "r");
    if(!f) return;
    if(fgets(b, sizeof b, f)){
        char v1[16], v2[16], v3[16];
        if(sscanf(b, "%15s %15s %15s", v1, v2, v3) == 3)
            snprintf(ZT_VER, sizeof ZT_VER, "%s", v3);
    }
    pclose(f);
}

/* 链路状态检测 (异步 peers): 仅读 zerotier-cli peers, 只做显示, 不做任何网络管理动作 */
static void link_start(void){
    if(LINK_BUSY) return;
    LINK_BUSY = 1;
    unlink(LINK_OUT); unlink(LINK_DONE);
    zt_find_dir();
    char cmd[256];
    if(ZT_DIR[0])
        snprintf(cmd, sizeof cmd,
                 "(zerotier-cli -D%s peers > %s 2>&1; touch %s) &",
                 ZT_DIR, LINK_OUT, LINK_DONE);
    else
        snprintf(cmd, sizeof cmd,
                 "(zerotier-cli peers > %s 2>&1; touch %s) &",
                 LINK_OUT, LINK_DONE);
    system(cmd);
}

/* poll: 返回 1 = 有新结果 */
static int link_poll(void){
    if(access(LINK_DONE, F_OK)) return 0;
    unlink(LINK_DONE); LINK_BUSY = 0;
    LINK_LAT = -1; LINK_TYPE[0] = 0;
    int has_direct = 0, has_relay = 0;
    int min_direct = -1, min_relay = -1;
    FILE *f = fopen(LINK_OUT, "r");
    if(!f){ unlink(LINK_OUT); return 1; }
    char ln[256];
    while(fgets(ln, sizeof ln, f)){
        /* 格式: <addr> <ver> <role> <lat> <link> ... */
        char a[16], v[16], r[16], la[16], lk[16];
        if(sscanf(ln, "%15s %15s %15s %15s %15s", a, v, r, la, lk) != 5) continue;
        int lat = (strcmp(la, "-") && atoi(la) > 0) ? atoi(la) : -1;
        if(!strcmp(lk, "DIRECT")){
            has_direct = 1;
            if(lat > 0 && (min_direct < 0 || lat < min_direct)) min_direct = lat;
        } else if(!strcmp(lk, "RELAY")){
            has_relay = 1;
            if(lat > 0 && (min_relay < 0 || lat < min_relay)) min_relay = lat;
        }
    }
    fclose(f);
    unlink(LINK_OUT);
    if(has_direct){ strcpy(LINK_TYPE, "DIRECT"); LINK_LAT = min_direct; }
    else if(has_relay){ strcpy(LINK_TYPE, "RELAY"); LINK_LAT = min_relay; }
    return 1;
}

/* 详情页显示值: "直连 56ms" / "中继" / "检测中" / "未连接" */
static void link_disp(char *out, int sz, char *clr){
    if(!strcmp(LINK_TYPE, "DIRECT")){
        if(LINK_LAT >= 0) snprintf(out, sz, "直连 %dms", LINK_LAT);
        else              snprintf(out, sz, "直连");
        *clr = 'g';
    } else if(!strcmp(LINK_TYPE, "RELAY")){
        if(LINK_LAT >= 0) snprintf(out, sz, "中继 %dms", LINK_LAT);
        else              snprintf(out, sz, "中继");
        *clr = 'y';
    } else if(LINK_BUSY || access(LINK_OUT, F_OK) == 0){
        snprintf(out, sz, "检测中");
        *clr = 'd';
    } else {
        snprintf(out, sz, "未连接");
        *clr = 'd';
    }
}

/* ---------- network list (本地) ---------- */
#define MAX_NETS 8

typedef struct {
    char id[20];
    char name[64];
    int  connected;
    char ip[32];
} ztnet_t;

static ztnet_t NETS[MAX_NETS];
static int N_NETS = 0;

static void zt_get_nets(void){
    N_NETS = 0;
    zt_find_dir();
    if(!ZT_DIR[0]) return;
    zt_get_ip();  /* 刷新 IP */
    char nd[256];
    snprintf(nd, sizeof nd, "%s/networks.d", ZT_DIR);
    DIR *d = opendir(nd);
    if(!d) return;
    struct dirent *de;
    while((de = readdir(d)) && N_NETS < MAX_NETS){
        if(de->d_name[0] == '.') continue;
        char path[256];
        snprintf(path, sizeof path, "%s/networks.d/%s", ZT_DIR, de->d_name);
        FILE *f = fopen(path, "r");
        if(!f) continue;
        ztnet_t *net = &NETS[N_NETS];
        memset(net, 0, sizeof *net);
        char ln[256];
        while(fgets(ln, sizeof ln, f)){
            if(!strncmp(ln, "nwid=", 5)){
                char *p = ln + 5, *nl = strchr(p, '\n');
                if(nl) *nl = 0;
                char *cr = strchr(p, '\r'); if(cr) *cr = 0;
                snprintf(net->id, sizeof net->id, "%s", p);
            } else if(!strncmp(ln, "n=", 2)){
                char *p = ln + 2, *nl = strchr(p, '\n');
                if(nl) *nl = 0;
                char *cr = strchr(p, '\r'); if(cr) *cr = 0;
                snprintf(net->name, sizeof net->name, "%s", p);
            } else if(!strncmp(ln, "ts=", 3)){
                char *v = ln + 3;
                while(*v == '0') v++;
                if(*v != '\n' && *v != '\r' && *v != ' ' && *v) net->connected = 1;
            }
        }
        fclose(f);
        if(!net->id[0]) continue;
        if(!net->name[0]) snprintf(net->name, sizeof net->name, "%s", net->id);
        if(ZT_IP[0]) snprintf(net->ip, sizeof net->ip, "%s", ZT_IP);
        else strcpy(net->ip, "--");
        N_NETS++;
    }
    closedir(d);
}

/* ---------- push to core ---------- */

static void push_card(void){
    mudi_card(ON && ZT_RUN);
    if(ON && ZT_RUN){
        char b[128];
        zt_get_nets();
        if(!ZT_ID[0]) zt_get_id(ZT_ID, sizeof ZT_ID);
        snprintf(b, sizeof b, "节点 %s", ZT_ID[0] ? ZT_ID : "--");
        mudi_cf("r2", b, 's');
        /* 优先显示 IP，其次网络名 */
        if(ZT_IP[0])
            snprintf(b, sizeof b, "%s", ZT_IP);
        else if(N_NETS > 0)
            snprintf(b, sizeof b, "%d 网络 · %s", N_NETS, NETS[0].name);
        else
            snprintf(b, sizeof b, "等待连接");
        mudi_cf("r3l", b, 's');
        if(N_NETS > 0 && NETS[0].connected)
            mudi_cf("r3r", "已连接", 'g');
        else if(N_NETS > 0)
            mudi_cf("r3r", "等待", 'y');
        else
            mudi_cf("r3r", "运行中", 'd');
        /* r4: 链路直连/中继状态 (圆点) */
        {
            char mdisp[32]; char mclr;
            link_disp(mdisp, sizeof mdisp, &mclr);
            snprintf(b, sizeof b, "● %s", mdisp);
            mudi_cf("r4", b, mclr);
        }
    } else if(ON && !ZT_RUN){
        mudi_cf("r2", "启动中...", 's');
        mudi_cf("r3l", "等待 ZeroTier 服务", 'f');
        mudi_cf("r3r", "", 'd');
        mudi_cf("r4", "", 'd');
    } else {
        mudi_cf("r2", "已停止", 'd');
        mudi_cf("r3l", "点按进入详情", 'f');
        mudi_cf("r3r", "", 'd');
        mudi_cf("r4", "", 'd');
    }
}

static void push_detail(void){
    if(!DETAIL_OPEN) return;
    /* 只做 mudi_set 更新，控制在 on_open 创建一次 */
    if(ON && !ZT_RUN){
        mudi_set("id", "等待中...", 'd');
        mudi_set("ver", "--", 's');
        mudi_set("nets_info", "--", 's');
        mudi_set("nets", "", 's');
        return;
    }
    mudi_set("id", ON && ZT_ID[0] ? ZT_ID : "--", 's');
    zt_get_ver();
    mudi_set("ver", ZT_VER[0] ? ZT_VER : "--", 's');
    char mdisp[32]; char mclr;
    link_disp(mdisp, sizeof mdisp, &mclr);
    mudi_set("link", mdisp, mclr);
    if(!ON){
        mudi_set("nets_info", "--", 's');
        mudi_set("nets", "", 's');
        return;
    }
    zt_get_nets();
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%d", N_NETS);
    mudi_set("nets_info", tmp, 's');
    mudi_set("nets", N_NETS > 0 ? "" : "无网络", 's');
}

static void push_net_list(void){
    if(!ZT_RUN){ mudi_list("nets", "网络不可用", NULL, NULL); mudi_lend("nets"); return; }
    zt_get_nets();
    mudi_list("nets", "ZeroTier 网络", NULL, NULL);
    for(int i = 0; i < N_NETS; i++){
        char label[128], sub[64];
        snprintf(label, sizeof label, "%s", NETS[i].name[0] ? NETS[i].name : NETS[i].id);
        if(NETS[i].ip[0] && strcmp(NETS[i].ip, "--"))
            snprintf(sub, sizeof sub, "%s", NETS[i].ip);
        else
            snprintf(sub, sizeof sub, "%s", "等待IP");
        mudi_item("nets", NETS[i].id, label, sub,
                  NETS[i].connected ? 'g' : 'y', 0);
    }
    mudi_lend("nets");
}

/* ================= lifecycle ================= */

static int  tick = 0;
static int  SVC_BUSY = 0, SVC_T = 0;

static void svc_toggle(int on){
    if(SVC_BUSY) return;
    SVC_BUSY = 1; SVC_T = 0;
    mudi_busy(1);
    ON = on;
    unlink("/tmp/mudi_svc.done");
    if(fork() == 0){
        if(on)
            system("/etc/init.d/zerotier start >/dev/null 2>&1; touch /tmp/mudi_svc.done");
        else
            system("/etc/init.d/zerotier stop >/dev/null 2>&1; touch /tmp/mudi_svc.done");
        _exit(0);
    }
}

static void on_init(void){
    ZT_RUN = zt_running();
    ON = ZT_RUN;
    if(ZT_RUN) zt_get_id(ZT_ID, sizeof ZT_ID);
    if(ZT_RUN) zt_get_ver();
    if(ZT_RUN) link_start();   /* 立即开始链路状态检测 */
    push_card();  /* INIT 后立刻推卡片数据 */
}

static void on_open(void){
    DETAIL_OPEN = 1;
    /* 不调 mudi_busy(0) — 可能正在 busy 中 */
    mudi_switch("svc", "服务", ON);
    mudi_info("id", "节点ID", ON && ZT_ID[0] ? ZT_ID : "--", 's');
    zt_get_ver();
    mudi_info("ver", "版本", ZT_VER[0] ? ZT_VER : "--", 's');
    mudi_info("link", "模式", "--", 's');
    mudi_info("nets_info", "网络数", "--", 's');
    mudi_menu("nets", "网络列表", "");
    push_card();
    push_detail();
    push_net_list();
}

static void on_close(void){ DETAIL_OPEN = 0; }

static void on_tick(int sec){
    tick = sec;
    /* ===== SVC_BUSY: 等启停完成 ===== */
    if(SVC_BUSY){
        SVC_T++;
        int done = access("/tmp/mudi_svc.done", F_OK) == 0;
        if(done || SVC_T > 15){
            SVC_BUSY = 0;
            ZT_RUN = ON ? zt_running() : 0;
            if(ZT_RUN){ zt_get_id(ZT_ID, sizeof ZT_ID); push_detail(); }
            push_card();
            mudi_checked("svc", ON);
            mudi_busy(0);
        }
        return;
    }
    /* ===== 后台发现服务启动 + 定期刷新 ===== */
    if(ON && !ZT_RUN && sec % 3 == 0){
        ZT_RUN = zt_running();
        if(ZT_RUN){
            zt_get_id(ZT_ID, sizeof ZT_ID);
            push_card();
            push_detail();
        }
    }
    if(sec % 5 == 0) push_card();
    if(sec % 5 == 1 && !LINK_BUSY) link_start();
    if(link_poll()){ if(DETAIL_OPEN) push_detail(); }
    if(DETAIL_OPEN && ZT_RUN) push_detail();  /* 每 tick 刷新 */
}

static void on_ev(const char *id, const char *arg){
    if(SVC_BUSY) return;
    if(!strcmp(id, "svc")){
        svc_toggle(arg[0] == '1');
        mudi_set("id", ZT_ID[0] ? ZT_ID : "--", 's');
    } else if(!strcmp(id, "@btn")){
        push_net_list();
    }
}

static mudi_plugin_t P = {
    .name     = "ZeroTier",
    .on_init  = on_init,
    .on_open  = on_open,
    .on_tick  = on_tick,
    .on_close = on_close,
    .on_ev    = on_ev,
};

int main(void){
    mudi_run(&P);
    return 0;
}
