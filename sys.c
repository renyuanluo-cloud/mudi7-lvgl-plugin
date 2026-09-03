#include "sys.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <time.h>

lv_color_t clr_delay(int ms){
    if(ms <= 0)   return lv_color_hex(0x666666);   /* 0ms 无效,灰 */
    if(ms < 200)  return lv_color_hex(0x3FB950);
    if(ms < 500)  return lv_color_hex(0xE3B341);
    if(ms < 1000) return lv_color_hex(0xF0883E);
    return lv_color_hex(0xF85149);
}
lv_color_t clr_pct(int p){
    if(p < 30) return lv_color_hex(0x3FB950);
    if(p < 60) return lv_color_hex(0xE3B341);
    if(p < 85) return lv_color_hex(0xF0883E);
    return lv_color_hex(0xF85149);
}
lv_color_t clr_batt(int p){
    if(p > 60) return lv_color_hex(0x3FB950);
    if(p > 30) return lv_color_hex(0xE3B341);
    if(p > 15) return lv_color_hex(0xF0883E);
    return lv_color_hex(0xF85149);
}

int sys_cpu(void){
    static unsigned long long pi, pt;
    unsigned long long u,n,s,i,iw,irq,sirq;
    char ln[256];
    FILE *f = fopen("/proc/stat","r");
    if(!f) return 0;
    if(!fgets(ln,sizeof ln,f)){ fclose(f); return 0; }
    fclose(f);
    u=n=s=i=iw=irq=sirq=0;
    sscanf(ln,"cpu %llu %llu %llu %llu %llu %llu %llu",&u,&n,&s,&i,&iw,&irq,&sirq);
    unsigned long long idle = i+iw, tot = u+n+s+i+iw+irq+sirq;
    int r = 0;
    if(pt && tot > pt){
        unsigned long long dt = tot-pt, di = idle-pi;
        if(dt > di) r = (int)(100*(dt-di)/dt);
    }
    pi = idle; pt = tot;
    if(r < 0) r = 0; if(r > 100) r = 100;
    return r;
}

int sys_ram(void){
    char ln[128]; unsigned long t=0, a=0;
    FILE *f = fopen("/proc/meminfo","r");
    if(!f) return 0;
    while(fgets(ln,sizeof ln,f)){
        if(!strncmp(ln,"MemTotal",8))      sscanf(ln+9,"%lu",&t);
        else if(!strncmp(ln,"MemAvailable",12)) sscanf(ln+13,"%lu",&a);
    }
    fclose(f);
    if(!t || !a || a > t) return 0;
    return (int)(100*(t-a)/t);
}

int sys_batt(void){
    static char path[128];
    if(!path[0]){
        DIR *d = opendir("/sys/class/power_supply");
        if(d){
            struct dirent *de;
            while((de = readdir(d))){
                if(de->d_name[0] == '.') continue;
                char tp[160], tb[32] = {0};
                snprintf(tp,sizeof tp,"/sys/class/power_supply/%s/type",de->d_name);
                FILE *f = fopen(tp,"r");
                if(f){ if(fgets(tb,sizeof tb,f)){} fclose(f); }
                if(!strncmp(tb,"Battery",7)){
                    snprintf(path,sizeof path,"/sys/class/power_supply/%s/capacity",de->d_name);
                    break;
                }
            }
            closedir(d);
        }
        if(!path[0]) strcpy(path,"none");
    }
    if(!strcmp(path,"none")) return -1;
    FILE *f = fopen(path,"r");
    if(!f) return -1;
    int v = -1;
    if(fscanf(f,"%d",&v) != 1) v = -1;
    fclose(f);
    if(v < 0) v = 0; if(v > 100) v = 100;
    return v;
}

void sys_net(unsigned *up, unsigned *down){
    static unsigned long long prx, ptx;
    static time_t pt0;
    unsigned long long rx = 0, tx = 0;
    FILE *f = fopen("/proc/net/dev","r");
    if(f){
        char ln[256];
        while(fgets(ln,sizeof ln,f)){
            char *c = strchr(ln,':');
            if(!c) continue;
            *c = ' ';
            char nm[16]; unsigned long long r,t,dum;
            if(sscanf(ln,"%15s %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                      nm,&r,&dum,&dum,&dum,&dum,&dum,&dum,&dum,&t) >= 3){
                if(strcmp(nm,"lo") && strncmp(nm,"ifb",3)){ rx += r; tx += t; }
            }
        }
        fclose(f);
    }
    time_t now = time(NULL);
    unsigned u = 0, d = 0;
    if(pt0 && now > pt0){
        d = (unsigned)((rx-prx)/(now-pt0));
        u = (unsigned)((tx-ptx)/(now-pt0));
    }
    prx = rx; ptx = tx; pt0 = now;
    *up = u; *down = d;
}
