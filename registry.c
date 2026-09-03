#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define CONF "/etc/mudi_ui.conf"
#define PDIR "/etc/mudi/plugins"

/* 从扁平 json 里抓 "key":"value" 或 "key":number */
static void jget(const char *buf, const char *key, char *out, int sz){
    char pat[48];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(buf, pat);
    out[0] = 0;
    if(!p) return;
    p = strchr(p + strlen(pat), ':');
    if(!p) return;
    p++;
    while(*p == ' ' || *p == '\t') p++;
    if(*p == '"'){
        p++;
        const char *q = strchr(p, '"');
        if(!q) return;
        int l = (int)(q - p) < sz-1 ? (int)(q - p) : sz-1;
        memcpy(out, p, l); out[l] = 0;
    } else {
        int i = 0;
        while((*p >= '0' && *p <= '9') && i < sz-1) out[i++] = *p++;
        out[i] = 0;
    }
}

static int conf_visible(const char *id, int dflt){
    FILE *f = fopen(CONF, "r");
    if(!f) return dflt;
    char ln[96], key[48];
    snprintf(key, sizeof key, "show_%s=", id);
    int v = dflt;
    while(fgets(ln, sizeof ln, f))
        if(!strncmp(ln, key, strlen(key))){ v = atoi(ln + strlen(key)); break; }
    fclose(f);
    return v;
}

int registry_scan(void){
    N_PL = 0;
    DIR *d = opendir(PDIR);
    if(!d) return 0;
    struct dirent *de;
    while((de = readdir(d)) && N_PL < MAX_PLUGINS){
        if(de->d_name[0] == '.') continue;
        char *dot = strrchr(de->d_name, '.');
        if(!dot || strcmp(dot, ".json")) continue;
        char path[224];
        snprintf(path, sizeof path, "%s/%s", PDIR, de->d_name);
        FILE *f = fopen(path, "r");
        if(!f) continue;
        char buf[1024];
        int n = (int)fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        if(n < 10) continue;
        buf[n] = 0;
        plugin_t *p = &PL[N_PL];
        memset(p, 0, sizeof *p);
        jget(buf, "id", p->m.id, sizeof p->m.id);
        jget(buf, "name", p->m.name, sizeof p->m.name);
        jget(buf, "exec", p->m.exec, sizeof p->m.exec);
        jget(buf, "version", p->m.version, sizeof p->m.version);
        jget(buf, "api", p->m.api, sizeof p->m.api);
        jget(buf, "permissions", p->m.permissions, sizeof p->m.permissions);
        if(!p->m.id[0] || !p->m.exec[0]) continue;
        if(!p->m.name[0]) strncpy(p->m.name, p->m.id, sizeof p->m.name - 1);
        p->visible = conf_visible(p->m.id, 1);
        N_PL++;
    }
    closedir(d);
    return N_PL;
}

void registry_save_visible(void){
    /* 保留 conf 里已有的其他键 (如 light=) */
    char keep[512] = {0};
    FILE *f = fopen(CONF, "r");
    if(f){
        char ln[96];
        while(fgets(ln, sizeof ln, f))
            if(strncmp(ln, "show_", 5)) strncat(keep, ln, sizeof(keep) - strlen(keep) - 1);
        fclose(f);
    }
    f = fopen(CONF, "w");
    if(!f) return;
    fputs(keep, f);
    for(int i = 0; i < N_PL; i++)
        fprintf(f, "show_%s=%d\n", PL[i].m.id, PL[i].visible);
    fclose(f);
}
