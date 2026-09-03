#include "oc.h"
#include "libplugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>

/* ================= 配置 (运行时从 OpenClash UCI/which 自动发现, 零 conf) ================= */

static char API_BASE[96]   = "http://127.0.0.1:9090";   /* 端口读 UCI cn_port (空→9090) */
static char SUB_SCRIPT[160] = "";                        /* which 探测订阅更新脚本路径 */
static char TEST_URL[256]  = "https://www.google.com/generate_204";  /* 功能常量: 节点批量测速目标 */
/* 连通性探测目标 = 功能常量 (YT/GH), 非环境配置, 不读任何 conf */
static char PROBE_NM[2][24]  = {{"YT"}, {"GH"}};
static char PROBE_URL[2][160] = {{"http://www.youtube.com"}, {"http://github.com"}};
static int  CONF_READ = 0;

/* 一次性发现全部连接参数 (幂等, 可多次调用) */
void oc_conf_init(void){
    if(CONF_READ) return;
    CONF_READ = 1;
    char v[64];
    FILE *f;
    /* API 端口: uci get openclash.config.cn_port (空/"0" → 默认 9090) */
    f = popen("uci -q get openclash.config.cn_port 2>/dev/null", "r");
    if(f){
        v[0] = 0;
        if(fgets(v, sizeof v, f)){}
        pclose(f);
        char *nl = strchr(v, '\n'); if(nl) *nl = 0;
        char *sp = strchr(v, ' ');  if(sp) *sp = 0;
        if(v[0] && strcmp(v, "0"))
            snprintf(API_BASE, sizeof API_BASE, "http://127.0.0.1:%s", v);
    }
    /* 订阅更新脚本: which 探测 (OpenClash ipk 安装路径; 找不到则该功能禁用) */
    f = popen("which openclash_update.sh 2>/dev/null", "r");
    if(f){
        SUB_SCRIPT[0] = 0;
        if(fgets(SUB_SCRIPT, sizeof SUB_SCRIPT, f)){}
        pclose(f);
        char *nl = strchr(SUB_SCRIPT, '\n'); if(nl) *nl = 0;
        char *sp = strchr(SUB_SCRIPT, ' ');  if(sp) *sp = 0;
    }
}

const char *oc_conf_api(void){ oc_conf_init(); return API_BASE; }
const char *oc_conf_sub(void){ oc_conf_init(); return SUB_SCRIPT; }
const char *oc_conf_test_url(void){ oc_conf_init(); return TEST_URL; }
const char *oc_probe_name(int idx){ oc_conf_init(); return (idx == 1) ? PROBE_NM[1] : PROBE_NM[0]; }
const char *oc_probe_url(int idx){ oc_conf_init(); return (idx == 1) ? PROBE_URL[1] : PROBE_URL[0]; }

/* ================= auth ================= */

static char AUTH[64];
static int  AUTH_DONE = 0;
static void auth_init(void){
    if(AUTH_DONE) return;
    AUTH_DONE = 1;
    /* 从 OpenClash UCI 读 API 口令; 未配置则 mihomo 为无鉴权模式, 不发头 */
    FILE *f = popen("uci -q get openclash.config.dashboard_password 2>/dev/null", "r");
    if(f){
        if(fgets(AUTH, sizeof AUTH, f)){}
        pclose(f);
    }
    char *e = strchr(AUTH, '\n'); if(e) *e = 0;
    char *sp = strchr(AUTH, ' '); if(sp) *sp = 0;   /* 防尾部空格 */
}

/* 鉴权头片段: 有口令 → " -H 'Authorization: Bearer xxx'"; 无 → 空 (mihomo 无鉴权) */
static void auth_hdr(char *out, int sz){
    auth_init();
    if(AUTH[0]) snprintf(out, sz, " -H 'Authorization: Bearer %s'", AUTH);
    else out[0] = 0;
}

static char *bs(const char *h, size_t n, const char *p){
    return (char *)memmem(h, n, p, strlen(p));
}

/* 返回匹配右括号位置 (字符串感知) */
static char *obj_end(char *o){
    int d = 0, instr = 0;
    for(char *q = o; *q; q++){
        if(*q == '"') instr = !instr;
        if(instr) continue;
        if(*q == '{') d++;
        else if(*q == '}'){ d--; if(!d) return q; }
    }
    return NULL;
}

/* http -> returns body length, 0 on fail */
static int http(const char *method, const char *path, const char *body, char *out, int outsz){
    char hdr[128], cmd[1400];
    auth_hdr(hdr, sizeof hdr);
    if(body)
        snprintf(cmd,sizeof cmd,"curl -s -m 8 -X %s%s -H 'Content-Type: application/json' -d '%s' '%s%s' 2>/dev/null",
                 method, hdr, body, oc_conf_api(), path);
    else
        snprintf(cmd,sizeof cmd,"curl -s -m 8 -X %s%s '%s%s' 2>/dev/null",
                 method, hdr, oc_conf_api(), path);
    FILE *f = popen(cmd,"r");
    if(!f) return 0;
    int n = 0;
    if(out){ n = (int)fread(out,1,outsz-1,f); out[n] = 0; }
    pclose(f);
    return n;
}

static void urlenc(const char *in, char *out, int sz){
    int o = 0;
    for(const unsigned char *p = (const unsigned char*)in; *p && o < sz-4; p++){
        if((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p=='-' || *p=='_' || *p=='.')
            out[o++] = *p;
        else o += snprintf(out+o, sz-o, "%%%02X", *p);
    }
    out[o] = 0;
}

static void spawn(const char *cmd){
    if(fork() == 0){
        system(cmd);
        _exit(0);
    }
}

/* ---------- basic state ---------- */

int oc_running(void){
    char b[256]; int n;
    for(int t = 0; t < 2; t++){  /* 重试一次防 curl 瞬时超时 */
        n = http("GET","/version",NULL,b,sizeof b);
        if(n > 5 && strstr(b,"version")) return 1;
        usleep(200000);
    }
    return 0;
}

int oc_mode(char *out, int sz){
    char b[3072];
    int n = http("GET","/configs",NULL,b,sizeof b);
    if(n < 10) return -1;
    char *p = strstr(b,"\"mode\":\"");
    if(!p) return -1;
    p += 8;
    char *q = strchr(p,'"');
    if(!q) return -1;
    int l = q-p; if(l >= sz) l = sz-1;
    memcpy(out,p,l); out[l] = 0;
    return 0;
}

void oc_set_mode(const char *mode){
    char body[64], cmd[400], hdr[128];
    snprintf(body,sizeof body,"{\"mode\":\"%s\"}",mode);
    auth_hdr(hdr, sizeof hdr);
    snprintf(cmd,sizeof cmd,
        "curl -s -m 5 -X PATCH%s -H 'Content-Type: application/json' -d '%s' '%s/configs' >/dev/null 2>&1",
        hdr, body, oc_conf_api());
    spawn(cmd);
}

/* find matching ] for the [ at start, with quote-awareness (skips [] inside strings) */
static char *find_array_end(char *open){
    int depth = 0;
    char *p = open;
    int in_str = 0;
    while(*p){
        if(*p == '\\' && in_str){ p++; if(*p) p++; continue; }  /* skip escaped char */
        if(*p == '"'){ in_str = !in_str; p++; continue; }
        if(!in_str){
            if(*p == '[') depth++;
            else if(*p == ']'){ depth--; if(depth == 0) return p; }
        }
        p++;
    }
    return NULL;
}

int oc_nodes(char grp[64], char cur[64], node_t *nd, int max){
    static char *buf;
    if(!buf) buf = malloc(360*1024);
    if(!buf) return -1;
    int n = http("GET","/proxies",NULL,buf,360*1024-1);
    if(n < 1000) return -1;
    char *end = buf + n;
    char *pp = bs(buf,n,"\"proxies\":{");
    if(!pp) return -1;
    pp += 10;

    char gnames[8][64], gnow[8][64], gtype[8][16];
    long gall_off[8];
    int gall_len[8], gcnt[8], ng = 0;

    char *scan = pp;
    int ng_dbg = 0;
    while(ng < 8){
        char *a = bs(scan,end-scan,"\"all\":[");
        if(!a) break;
        /* backtrack to enclosing object start */
        char *o = a; int d = 0;
        while(o > pp){ if(*o=='}') d++; else if(*o=='{'){ if(d==0) break; d--; } o--; }
        /* key name before '{' */
        char nm[64] = {0};
        char *k = o-1;
        while(k > pp && *k != '"') k--;
        char *ke = k; k--;
        while(k > pp && *k != '"') k--;
        int kl = (int)(ke-k-1);
        if(kl > 0 && kl < 64){ memcpy(nm,k+1,kl); nm[kl] = 0; }
        /* now/type 在 "all" 之后(字母序), 必须搜整个对象范围 */
        char *oe = obj_end(o);
        if(!oe) break;
        char tv[16] = {0};
        char *ty = bs(o,oe-o,"\"type\":\"");
        if(ty){ ty += 8; char *q = strchr(ty,'"'); if(q){ int l = q-ty<15?(int)(q-ty):15; memcpy(tv,ty,l);} }
        char nw[64] = {0};
        char *np = bs(o,oe-o,"\"now\":\"");
        if(np){ np += 7; char *q = strchr(np,'"'); if(q){ int l = q-np<63?(int)(q-np):63; memcpy(nw,np,l);} }
        /* all span + 成员计数 */
        char *ae = find_array_end(a + 6);  /* 从 [ 开始, 让函数自增 depth */
        if(!ae) break;
        int cnt = 0;
        for(char *c = a+7; c < ae; c++) if(*c == '"') cnt++;
        cnt /= 2;

        if(nm[0] && strcmp(nm,"GLOBAL")){
            strcpy(gnames[ng], nm);
            strcpy(gnow[ng], nw);
            strcpy(gtype[ng], tv);
            gall_off[ng] = (a+7) - buf;
            gall_len[ng] = (int)(ae-a-7);
            gcnt[ng] = cnt;
            ng++;
        }
        scan = ae + 1;   /* 跳过整个 "all":[...] 数组, 不是只跳到 [ */
    }
    /* 主代理组 = 成员最多的 Selector, 无 Selector 退而取成员最多的组 */
    int best = -1;
    for(int i = 0; i < ng; i++)
        if(!strcmp(gtype[i],"Selector") && (best < 0 || gcnt[i] > gcnt[best])) best = i;
    if(best < 0)
        for(int i = 0; i < ng; i++)
            if(best < 0 || gcnt[i] > gcnt[best]) best = i;
    if(best < 0) return -1;

    static char gall[12000];
    int al = gall_len[best] > (int)sizeof(gall)-1 ? (int)sizeof(gall)-1 : gall_len[best];
    memcpy(gall, buf + gall_off[best], al); gall[al] = 0;
    strcpy(grp, gnames[best]);
    char selnow[64];
    strcpy(selnow, gnow[best]);

    /* resolve leaf node */
    strcpy(cur, selnow);
    for(int t = 0; t < 3; t++){
        int i;
        for(i = 0; i < ng; i++)
            if(!strcmp(cur,gnames[i])){ strcpy(cur,gnow[i]); break; }
        if(i == ng) break;
    }
    /* 若 cur 为空或指向 metadata 条目, 回退到第一个真实叶子节点 */
    int is_invalid = !cur[0];
    if(!is_invalid){
        /* 检查 cur 是否在 gnames 里(没解析完) */
        for(int i = 0; i < ng; i++) if(!strcmp(cur, gnames[i])){ is_invalid = 1; break; }
        /* 检查 metadata */
        if(!is_invalid){
            if(strstr(cur, "流量") || strstr(cur, "重置") ||
               strstr(cur, "套餐") || strstr(cur, "跳转") ||
               strstr(cur, "剩余")) is_invalid = 1;
        }
    }
    if(is_invalid){
        /* 在原始 all 数组里找第一个非 group / 非 metadata 的名字 */
        char *p = gall;
        int found = 0;
        while(*p){
            while(*p && *p != '"') p++;
            if(!*p) break; p++;
            char *q = strchr(p,'"');
            if(!q) break;
            int l = (int)(q-p) > 63 ? 63 : (int)(q-p);
            char nm[64] = {0};
            memcpy(nm, p, l);
            int bad = 0;
            for(int i = 0; i < ng; i++) if(!strcmp(nm, gnames[i])){ bad = 1; break; }
            if(!bad && strstr(nm, "流量") == NULL && strstr(nm, "重置") == NULL &&
               strstr(nm, "套餐") == NULL && strstr(nm, "跳转") == NULL &&
               strstr(nm, "剩余") == NULL && nm[0]){
                strcpy(cur, nm); found = 1; break;
            }
            p = q+1;
        }
    }

    /* parse names + delays */
    int cnt = 0;
    char *p = gall;
    int total_seen = 0;
    int real_added = 0;
    while(*p && cnt < max){
        while(*p && *p != '"') p++;
        if(!*p) break;
        p++;
        char *q = strchr(p,'"');
        if(!q) break;
        int l = (int)(q-p) > 63 ? 63 : (int)(q-p);
        memcpy(nd[cnt].name,p,l); nd[cnt].name[l] = 0;
        p = q+1;
        total_seen++;
        int isg = 0;
        for(int i = 0; i < ng; i++)
            if(!strcmp(nd[cnt].name,gnames[i])){ isg = 1; break; }
        if(isg) continue;
        /* 跳过 metadata 条目(流量/重置/套餐/跳转/剩余——订阅配置注入的信息条目, 非真实节点) */
        int is_meta = strstr(nd[cnt].name, "流量") || strstr(nd[cnt].name, "重置") ||
                      strstr(nd[cnt].name, "套餐") || strstr(nd[cnt].name, "跳转") ||
                      strstr(nd[cnt].name, "剩余");
        if(is_meta) continue;
        /* last delay in history */
        nd[cnt].delay = -1;
        char pat[80];
        snprintf(pat,sizeof pat,"\"%s\":{",nd[cnt].name);
        char *no = bs(buf,n,pat);
        if(no){
            char *hs = bs(no,end-no,"\"history\":[");
            if(hs){
                char *he = bs(hs,end-hs,"]");
                if(he){
                    char *s2 = hs, *occ;
                    while((occ = bs(s2,he-s2,"\"delay\":"))){ nd[cnt].delay = atoi(occ+8); s2 = occ+8; }
                }
            }
        }
        cnt++;
        real_added++;
    }
    return cnt;
}

void oc_select(const char *grp, const char *name){
    char eg[256], body[320], cmd[900], hdr[128];
    urlenc(grp,eg,sizeof eg);
    auth_hdr(hdr, sizeof hdr);
    snprintf(body,sizeof body,"{\"name\":\"%s\"}",name);
    snprintf(cmd,sizeof cmd,
        "curl -s -m 5 -X PUT%s -H 'Content-Type: application/json' -d '%s' '%s/proxies/%s' >/dev/null 2>&1",
        hdr, body, oc_conf_api(), eg);
    spawn(cmd);
}

void oc_job_test_start(const char *grp){
    char eg[256], cmd[1800], hdr[128], u[800];
    urlenc(grp,eg,sizeof eg);
    urlenc(oc_conf_test_url(), u, sizeof u);
    auth_hdr(hdr, sizeof hdr);
    unlink("/tmp/mudi_test.done");
    unlink("/tmp/mudi_test.json");
    snprintf(cmd,sizeof cmd,
        "curl -s -m 60%s '%s/group/%s/delay?url=%s&timeout=5000' -o /tmp/mudi_test.json 2>/dev/null; touch /tmp/mudi_test.done",
        hdr, oc_conf_api(), eg, u);
    spawn(cmd);
}

int oc_job_test_poll(void){
    if(!access("/tmp/mudi_test.done",F_OK)){ unlink("/tmp/mudi_test.done"); return 1; }
    return 0;
}

/* 从 /tmp/mudi_test.json 读取测速结果并更新 ND 延迟 */
int oc_test_apply(node_t *nd, int n_nd){
    FILE *f = fopen("/tmp/mudi_test.json","r");
    if(!f) return 0;
    char buf[128*1024];
    int n = (int)fread(buf,1,sizeof buf-1,f); buf[n] = 0;
    fclose(f);
    if(strstr(buf,"all proxies timeout") || strstr(buf,"message")) return 0;
    /* {"HKG B":369,"HKG D":209,...} 简单 key":value 解析 */
    char *p = buf;
    int updated = 0;
    while(*p){
        if(*p != '"'){ p++; continue; }
        p++; /* skip opening " */
        char *kend = strchr(p,'"');
        if(!kend || *(kend+1) != ':') break;
        int klen = (int)(kend-p) > 63 ? 63 : (int)(kend-p);
        char key[64]; memcpy(key,p,klen); key[klen] = 0;
        p = kend+2; /* skip ":*/
        int val = atoi(p);
        for(int i = 0; i < n_nd; i++)
            if(!strcmp(nd[i].name, key)){ nd[i].delay = val; updated++; break; }
        /* skip to next */
        while(*p && *p != ',') p++;
        if(*p == ',') p++;
    }
    return updated;
}

/* ---------- configs ---------- */

int oc_cfgs(char cur[64], char names[][64], int max){
    cur[0] = 0;
    FILE *f = fopen("/etc/config/openclash","r");
    if(f){
        char ln[256];
        while(fgets(ln,sizeof ln,f)){
            if(strstr(ln,"config_path")){
                char *p = strrchr(ln,'/');
                if(p){
                    strncpy(cur,p+1,63); cur[63] = 0;
                    char *e = strstr(cur,".yaml"); if(e) *e = 0;
                    char *q = cur; while(*q && (*q=='\'' || *q=='"' || *q==' ')) q++;
                    if(q != cur) memmove(cur,q,strlen(q)+1);
                }
                break;
            }
        }
        fclose(f);
    }
    int cnt = 0;
    DIR *d = opendir("/etc/openclash/config");
    if(d){
        struct dirent *de;
        while((de = readdir(d)) && cnt < max){
            if(de->d_name[0] == '.') continue;
            char *y = strstr(de->d_name,".yaml");
            if(y && y[5] == 0){
                strncpy(names[cnt],de->d_name,63); names[cnt][63] = 0;
                char *e = strstr(names[cnt],".yaml"); *e = 0;
                cnt++;
            }
        }
        closedir(d);
    }
    return cnt;
}

void oc_cfg_switch(const char *name){
    unlink("/tmp/mudi_svc.done");
    char cmd[600];
    snprintf(cmd,sizeof cmd,
        "uci set openclash.config.config_path='/etc/openclash/config/%s.yaml'; uci commit openclash; /etc/init.d/openclash restart >/dev/null 2>&1; touch /tmp/mudi_svc.done",
        name);
    spawn(cmd);
}

/* ---------- service & connectivity jobs ---------- */

void oc_job_svc_start(int on){
    unlink("/tmp/mudi_svc.done");
    char cmd[400];
    if(on)
        snprintf(cmd,sizeof cmd,
            "uci set openclash.config.enable=1; uci commit openclash; /etc/init.d/openclash start >/dev/null 2>&1; touch /tmp/mudi_svc.done");
    else
        snprintf(cmd,sizeof cmd,
            "/etc/init.d/openclash stop >/dev/null 2>&1; uci set openclash.config.enable=0; uci commit openclash; touch /tmp/mudi_svc.done");
    spawn(cmd);
}

int oc_job_svc_poll(void){
    if(!access("/tmp/mudi_svc.done",F_OK)){ unlink("/tmp/mudi_svc.done"); return 1; }
    return 0;
}

void oc_job_conn_start(const char *group){
    unlink("/tmp/mudi_conn.done");
    unlink("/tmp/mudi_conn.val");
    /* Mihomo API delay 测两个探测目标 (默认 YT/GH) 可达性 (经代理) */
    char cmd[1800], hdr[128];
    char p0[520], p1[520];
    urlenc(oc_probe_url(0), p0, sizeof p0);
    urlenc(oc_probe_url(1), p1, sizeof p1);
    auth_hdr(hdr, sizeof hdr);
    snprintf(cmd, sizeof cmd,
        "( YT=$(curl -s%s \"%s/proxies/%s/delay?timeout=5000&url=%s\"); "
        "GH=$(curl -s%s \"%s/proxies/%s/delay?timeout=5000&url=%s\"); "
        "echo \"$YT $GH\" > /tmp/mudi_conn.val; touch /tmp/mudi_conn.done ) >/dev/null 2>&1",
        hdr, oc_conf_api(), group, p0, hdr, oc_conf_api(), group, p1);
    spawn(cmd);
}

int oc_job_conn_poll(int *yt, int *gh){
    if(access("/tmp/mudi_conn.done",F_OK)) return 0;
    unlink("/tmp/mudi_conn.done");
    *yt = *gh = -1;
    FILE *f = fopen("/tmp/mudi_conn.val","r");
    if(f){
        char yt_raw[128] = {0}, gh_raw[128] = {0};
        if(fscanf(f,"%127s %127s", yt_raw, gh_raw) >= 1){
            /* Mihomo API 返回 {\"delay\":NNN} 或 {\"message\":\"...\"} */
            char *p;
            if((p = strstr(yt_raw, "\"delay\":")))  *yt = atoi(p+8);
            if((p = strstr(gh_raw, "\"delay\":")))  *gh = atoi(p+8);
        }
        fclose(f);
    }
    unlink("/tmp/mudi_conn.val");
    return 1;
}
