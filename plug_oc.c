/*
 * plug_oc.c — OpenClash 插件 (协议 v2 完整版)
 * 对象: 服务开关 / 状态 / YT / GH / 模式 / 配置 / 节点(列表+测速) / 重启
 * 本进程不含任何 UI/LVGL 代码, 只提供状态与动作。
 */
#include "libplugin.h"
#include "oc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 过滤节点名中的 4-byte UTF-8 字符(emoji/特殊符号), LVGL 8 freetype 无法渲染, 会显示为框框。
   4-byte UTF-8: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx (F0-F7 开头) */
static void strip_flags(char *s){
    unsigned char *p = (unsigned char *)s, *q = (unsigned char *)s;
    while(*p){
        if((*p & 0xF8) == 0xF0){  /* 4-byte UTF-8 start */
            p += 4; continue;
        }
        *q++ = *p++;
    }
    *q = 0;
    /* trim 首位空格 */
    char *t = s; while(*t == ' ') t++;
    if(t != s) memmove(s, t, strlen(t)+1);
}

static int ON = 0;
static int SVC_BUSY = 0, CFG_BUSY = 0, TEST_BUSY = 0, CONN_BUSY = 0, SUB_BUSY = 0;
static int SVC_T = 0, CFG_T = 0, TEST_T = 0, SUB_T = 0;
static int DETAIL_OPEN = 0;

static char MODE[16] = "--";       /* 显示用 (首字母大写) */
static char MODE_L[16] = "rule";   /* API 值 */
static char CFG[64] = "--";

static node_t ND[64];
static int  N_ND = 0;
static char GRP[64] = "";
static char CUR[64] = "";

static int CONN_YT = -2, CONN_GH = -2;   /* -2 未知, -1 失败 */

/* ---------- helpers ---------- */

static char dclr(int ms){
    if(ms <= 0)  return 's';   /* 未测 → secondary(可见) */
    if(ms < 200)  return 'g';
    if(ms < 500)  return 'y';
    if(ms < 1000) return 'o';
    return 'r';
}
static void fmt_ms(int ms, char *out, int sz){
    if(ms <= 0) snprintf(out, sz, "--");   /* 0ms 也是无效(不可能, 来自 mihomo 占位值) */
    else snprintf(out, sz, "%dms", ms);
}
static int cur_delay(void){
    for(int i = 0; i < N_ND; i++)
        if(!strcmp(ND[i].name, CUR)) return ND[i].delay;
    return -1;
}

static void restart_job(void){
    if(fork() == 0){
        system("/etc/init.d/openclash restart >/dev/null 2>&1; touch /tmp/mudi_svc.done");
        _exit(0);
    }
}

/* 订阅更新脚本是否可用 (which 探测结果为空 = OpenClash 未装该脚本, 禁用更新功能) */
static int sub_avail(void){ return oc_conf_sub()[0] != 0; }

static void refresh_state(void){
    ON = oc_running();
    if(ON){
        if(oc_mode(MODE_L, sizeof MODE_L) < 0) strcpy(MODE_L, "rule");
        snprintf(MODE, sizeof MODE, "%s", MODE_L);
        MODE[0] = (MODE[0] >= 'a' && MODE[0] <= 'z') ? MODE[0] - 32 : MODE[0];
        char names[16][64];
        if(oc_cfgs(CFG, names, 16) <= 0 || !CFG[0]) strcpy(CFG, "--");
        N_ND = oc_nodes(GRP, CUR, ND, 64);
        /* 保留原始节点名, strip 只在显示层做 */
        if(N_ND < 0) N_ND = 0;
    }
}

/* ---------- push (plugin -> core) ---------- */

static void push_card(void){
    mudi_card(ON);
    if(ON){
        char b[128];
        snprintf(b, sizeof b, "%s · %s", MODE, CFG);
        mudi_cf("r2", b, 's');
        /* 延迟: 优先节点测速, 回退到 YT 连通时间(节点通了 YT 必通, vice versa) */
        int d = cur_delay();
        if(d <= 0 && CONN_YT >= 0) d = CONN_YT;
        if(d <= 0 && CONN_GH >= 0) d = CONN_GH;
        char dly[16]; fmt_ms(d, dly, sizeof dly);
        char disp[64]; strcpy(disp, CUR[0] ? CUR : "--"); strip_flags(disp);
        snprintf(b, sizeof b, "%s · %s", disp, dly);
        mudi_cf("r3l", b, dclr(d));
        if(CONN_YT != -2){
            char a[16] = "--", c[16] = "--";
            if(CONN_YT >= 0) snprintf(a, sizeof a, "%dms", CONN_YT);
            if(CONN_GH >= 0) snprintf(c, sizeof c, "%dms", CONN_GH);
            snprintf(b, sizeof b, "%s %s · %s %s",
                     oc_probe_name(0), a, oc_probe_name(1), c);
            mudi_cf("r4", b, dclr(CONN_YT > CONN_GH ? CONN_YT : CONN_GH));
        } else mudi_cf("r4", "", 'd');
        mudi_cf("r3r", "", 'd');
    } else {
        mudi_cf("r2", "已停止", 'd');
        mudi_cf("r3l", "点按进入详情 ›", 'f');
        mudi_cf("r3r", "", 'd');
        mudi_cf("r4", "", 'd');
    }
}

static void push_mode_list(void){
    static const char *ids[3] = {"rule","global","direct"};
    static const char *names[3] = {"Rule","Global","Direct"};
    mudi_list("mode", "模式", NULL, NULL);
    for(int i = 0; i < 3; i++)
        mudi_item("mode", ids[i], names[i], "", 'n', !strcmp(MODE_L, ids[i]));
    mudi_lend("mode");
}

static void push_cfg_list(void){
    char cur[64]; char names[16][64];
    int n = oc_cfgs(cur, names, 16);
    mudi_list("cfg", "配置", sub_avail() ? "全更新" : NULL, NULL);
    for(int i = 0; i < n; i++)
        mudi_item("cfg", names[i], names[i], "", 'n', !strcmp(names[i], CFG));
    mudi_lend("cfg");
}

static void push_nodes_list(void){
    mudi_list("nodes", "节点选择", "测速", sub_avail() ? "更新" : NULL);
    for(int i = 0; i < N_ND; i++){
        char d[16], nm[64]; fmt_ms(ND[i].delay, d, sizeof d);
        strcpy(nm, ND[i].name); strip_flags(nm);
        mudi_item("nodes", ND[i].name, nm, d, dclr(ND[i].delay),
                  !strcmp(ND[i].name, CUR));
    }
    mudi_lend("nodes");
}

static void push_lists(void){
    if(!ON){
        mudi_list("mode", "模式", "", "");
        mudi_item("mode", "mode/--", "待开启", "", 'd', 0); mudi_lend("mode");
        mudi_list("cfg", "配置", "", "");
        mudi_item("cfg", "cfg/--", "待开启", "", 'd', 0); mudi_lend("cfg");
        mudi_list("nodes", "节点", "", "");
        mudi_item("nodes", "nodes/--", "待开启", "", 'd', 0); mudi_lend("nodes");
        return;
    }
    push_mode_list();
    push_cfg_list();
    push_nodes_list();
}

static void push_detail(void){
    if(!DETAIL_OPEN) return;
    mudi_set("run", ON ? "运行中" : "已停止", ON ? 'g' : 'd');
    if(ON){
        char d[16];
        if(CONN_YT != -2){ fmt_ms(CONN_YT, d, sizeof d); mudi_set("yt", d, dclr(CONN_YT)); }
        else mudi_set("yt", "--", 'd');
        if(CONN_GH != -2){ fmt_ms(CONN_GH, d, sizeof d); mudi_set("gh", d, dclr(CONN_GH)); }
        else mudi_set("gh", "--", 'd');
        mudi_set("mode", MODE, 'n');
        mudi_set("cfg", CFG, 'n');
        char v[128]; fmt_ms(cur_delay(), d, sizeof d);
        char disp[64]; strcpy(disp, CUR[0] ? CUR : "--"); strip_flags(disp);
        snprintf(v, sizeof v, "%s · %s", disp, d);
        mudi_set("nodes", v, dclr(cur_delay()));
    } else {
        mudi_set("yt", "--", 'd');
        mudi_set("gh", "--", 'd');
        mudi_set("mode", "--", 'd');
        mudi_set("cfg", "--", 'd');
        mudi_set("nodes", "--", 'd');
    }
}

static void push_all(void){
    mudi_checked("svc", ON);
    push_card();
    push_lists();
    push_detail();
}

/* ---------- callbacks ---------- */

static void on_init(void){
    refresh_state();
    push_card();
}

static void on_open(void){
    DETAIL_OPEN = 1;
    mudi_switch("svc", "服务", ON);
    mudi_info("run", "状态", ON ? "运行中" : "已停止", ON ? 'g' : 'd');
    mudi_info("yt", oc_probe_name(0), "--", 'd');
    mudi_info("gh", oc_probe_name(1), "--", 'd');
    mudi_menu("mode", "模式", ON ? MODE : "--");
    mudi_menu("cfg", "配置", ON ? CFG : "--");
    char v[128] = "--";
    if(ON && CUR[0]){
        char d[16]; fmt_ms(cur_delay(), d, sizeof d);
        snprintf(v, sizeof v, "%s · %s", CUR, d);
    }
    mudi_menu("nodes", "节点", v);
    push_lists();  /* ON/OFF 都注册列表, OFF 时显示占位符 */
    if(ON){
        /* 打开详情页时立即补一次连通性 */
        if(CONN_YT == -2 && !CONN_BUSY){ CONN_BUSY = 1; oc_job_conn_start(GRP); }
    }
}

static void on_close(void){ DETAIL_OPEN = 0; }

static void on_ev(const char *id, const char *arg){
    if(!strcmp(id, "svc")){
        if(SVC_BUSY) return;
        SVC_BUSY = 1;
        mudi_busy(1);
        oc_job_svc_start(arg[0] == '1');
        return;
    }
    if(!strcmp(id, "restart")){
        if(SVC_BUSY || !ON) return;
        SVC_BUSY = 1; SVC_T = 0;
        mudi_busy(1);
        unlink("/tmp/mudi_svc.done");
        restart_job();
        return;
    }
    if(!ON) return;   /* 以下操作都需要运行中 */

    if(!strcmp(id, "update_sub")){
        if(SUB_BUSY || !sub_avail()) return;
        SUB_BUSY = 1; SUB_T = 0;
        mudi_busy(1);
        char cmd[320];
        snprintf(cmd, sizeof cmd, "%s >/dev/null 2>&1 &", oc_conf_sub());
        system(cmd);
        return;
    }

    if(!strcmp(id, "mode")){
        if(!strcmp(arg, MODE_L)) return;
        char old[16]; strcpy(old, MODE_L);
        strcpy(MODE_L, arg);
        snprintf(MODE, sizeof MODE, "%s", MODE_L);
        MODE[0] = (MODE[0] >= 'a' && MODE[0] <= 'z') ? MODE[0] - 32 : MODE[0];
        oc_set_mode(MODE_L);                 /* 亚秒级, 无需遮罩 */
        mudi_set("mode", MODE, 'n');
        /* 选中标记: old=0 new=1 */
        printf("SET\tmode/%s\tchecked=0\nSET\tmode/%s\tchecked=1\n", old, MODE_L);
        fflush(stdout);
        return;
    }
    if(!strcmp(id, "cfg")){
        if(!strcmp(arg, CFG) || CFG_BUSY) return;
        CFG_BUSY = 1; CFG_T = 0;
        mudi_busy(1);
        oc_cfg_switch(arg);                  /* ~20s, done 文件驱动 */
        return;
    }
    if(!strcmp(id, "nodes")){
        if(!strcmp(arg, CUR)) return;
        char old[64]; strcpy(old, CUR);
        strcpy(CUR, arg);
        oc_select(GRP, CUR);
        /* 切换后立即刷 YT/GH 连通 */
        if(!CONN_BUSY){ CONN_BUSY = 1; oc_job_conn_start(GRP); }
        push_card();
        push_detail();
        printf("SET\tnodes/%s\tchecked=0\nSET\tnodes/%s\tchecked=1\n", old, CUR);
        fflush(stdout);
        return;
    }
}

/* libplugin 把 list 头按钮事件送为 id=<list>, arg=@btn */
static void on_ev2(const char *id, const char *arg){
    if(!strcmp(arg, "@btn") && !strcmp(id, "nodes")){
        if(!ON || TEST_BUSY) return;
        TEST_BUSY = 1; TEST_T = 0;
        mudi_busy(1);
        oc_job_test_start(GRP);
        return;
    }
    if(!strcmp(arg, "@btn") && !strcmp(id, "cfg")){
        if(!ON || SUB_BUSY || !sub_avail()) return;
        SUB_BUSY = 1; SUB_T = 0;
        mudi_busy(1);
        char cmd[320];
        snprintf(cmd, sizeof cmd, "%s >/dev/null 2>&1 &", oc_conf_sub());
        system(cmd);
        return;
    }
    if(!strcmp(arg, "@btn2") && !strcmp(id, "nodes")){
        if(!ON || SUB_BUSY || !sub_avail()) return;
        SUB_BUSY = 1; SUB_T = 0;
        mudi_busy(1);
        char cmd[320];
        snprintf(cmd, sizeof cmd, "%s %s >/dev/null 2>&1 &", oc_conf_sub(), CFG);
        system(cmd);
        return;
    }
    on_ev(id, arg);
}

static void on_tick(int sec){
    if(SVC_BUSY && oc_job_svc_poll()){
        SVC_BUSY = 0;
        SVC_T = 0;
        refresh_state();
        push_all();
        mudi_busy(0);
        return;
    }
    /* SVC 超时兜底 45s */
    if(SVC_BUSY && ++SVC_T > 45){
        SVC_BUSY = 0; SVC_T = 0;
        refresh_state();
        push_all();
        mudi_busy(0);
        return;
    }
    if(SVC_BUSY) return;
    if(CFG_BUSY){
        CFG_T++;
        if(oc_job_svc_poll() || CFG_T > 30){   /* 超时主动刷新兜底 */
            CFG_BUSY = 0;
            refresh_state();
            push_all();
            /* 切换配置后自动测速一次: 节点延迟立即可见, 无需手动点测速 */
            if(ON && !TEST_BUSY && N_ND > 0){
                TEST_BUSY = 1; TEST_T = 0;
                oc_job_test_start(GRP);   /* busy 遮罩保持到测速完成 */
            } else {
                mudi_busy(0);
            }
        }
        return;
    }
    if(TEST_BUSY){
        TEST_T++;
        if(oc_job_test_poll() || TEST_T > 70){ /* curl -m 60 + margin */
            TEST_BUSY = 0;
            N_ND = oc_nodes(GRP, CUR, ND, 64);
            if(N_ND < 0) N_ND = 0;
            oc_test_apply(ND, N_ND);  /* 优先测速文件直接结果 */
            push_nodes_list();
            push_detail();
            push_card();
            mudi_busy(0);
        }
        return;
    }
    /* 订阅更新轮询: 锁文件消失=完成, 45s 超时兜底 */
    if(SUB_BUSY){
        SUB_T++;
        if(access("/tmp/lock/openclash_update.lock", F_OK) != 0 || SUB_T > 45){
            SUB_BUSY = 0;
            refresh_state();
            push_all();
            mudi_busy(0);
        }
        return;
    }
    /* YT/GH 5s 轮询 */
    if(ON && !CONN_BUSY && (sec % 5 == 1)){
        CONN_BUSY = 1;
        oc_job_conn_start(GRP);
    }
    if(CONN_BUSY){
        int yt, gh;
        if(oc_job_conn_poll(&yt, &gh)){
            CONN_BUSY = 0;
            CONN_YT = yt; CONN_GH = gh;
            if(DETAIL_OPEN){
                char d[16];
                fmt_ms(yt, d, sizeof d); mudi_set("yt", d, dclr(yt));
                fmt_ms(gh, d, sizeof d); mudi_set("gh", d, dclr(gh));
            }
            push_card();
            push_detail();
        }
    }
    /* 节点数据为空时重试 (API 启动初期未就绪) */
    if(ON && N_ND == 0 && (sec % 3 == 0)){
        N_ND = oc_nodes(GRP, CUR, ND, 64);
        /* 保留原始节点名, strip 只在显示层做 */
        if(N_ND < 0) N_ND = 0;
        if(N_ND > 0){
            push_nodes_list();
            push_detail();
            push_card();
        }
    }
    /* 周期性轻量刷新: 每 3s 检查模式/配置变化，每 5s 检查节点变化 */
    if(ON && !SVC_BUSY && !CFG_BUSY && !TEST_BUSY && !CONN_BUSY){
        if(sec % 3 == 0){
            /* 检查模式变化(轻量, 小 API) */
            char m[16]; int rm = oc_mode(m, sizeof m);
            if(rm >= 0 && strcmp(m, MODE_L)){
                strcpy(MODE_L, m);
                snprintf(MODE, sizeof MODE, "%s", MODE_L);
                MODE[0] = (MODE[0] >= 'a' && MODE[0] <= 'z') ? MODE[0] - 32 : MODE[0];
                mudi_set("mode", MODE, 'n');
            }
            /* 检查配置变化(读文件) */
            char c[64]; int nc = oc_cfgs(c, NULL, 0);
            if(nc >= 0 && strcmp(c, CFG)){ strcpy(CFG, c); mudi_set("cfg", CFG, 'n'); }
            push_card();
        }
        if(sec % 5 == 0){
            /* 检查当前节点变化(轻量, 只取当前 now, 不取全部节点) */
            N_ND = oc_nodes(GRP, CUR, ND, 64);
            if(N_ND > 0){ push_detail(); push_card(); push_nodes_list(); }
        }
    }
    /* 状态同步 2s */
    if(!SVC_BUSY && !CFG_BUSY && !TEST_BUSY && (sec % 2 == 0)){
        int r = oc_running();
        if(r != ON){
            refresh_state();
            if(!ON) CONN_YT = CONN_GH = -2;
            push_all();
        }
    }
}

int main(void){
    static mudi_plugin_t p = {
        .name = "OpenClash",
        .on_init = on_init,
        .on_open = on_open,
        .on_ev = on_ev2,
        .on_tick = on_tick,
        .on_close = on_close,
    };
    return mudi_run(&p);
}
