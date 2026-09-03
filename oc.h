#ifndef MUDI_OC_H
#define MUDI_OC_H

typedef struct {
    char name[64];
    int  delay;   /* -1 = untested */
} node_t;

/* 配置 (运行时自动发现: 端口=UCI cn_port, 密码=UCI dashboard_password, 订阅脚本=which) */
void oc_conf_init(void);                       /* 发现连接参数 (幂等, 可多次调用) */
const char *oc_conf_api(void);                 /* 完整 API base, 如 http://127.0.0.1:9090 */
const char *oc_conf_sub(void);                 /* 订阅更新脚本路径 (which 探测, 空=不可用) */
const char *oc_probe_name(int idx);            /* 连通性探测显示名 (功能常量), idx=0/1 */
const char *oc_probe_url(int idx);             /* 连通性探测 URL (功能常量), idx=0/1 */
const char *oc_conf_test_url(void);            /* 节点测速 URL (功能常量, 需已 urlencode) */

/* sync state query */
int  oc_running(void);                     /* 1/0 via API /version */
int  oc_mode(char *out, int sz);           /* rule/global/direct, -1 err */
void oc_set_mode(const char *mode);        /* async, fire-and-forget */
int  oc_nodes(char grp[64], char cur[64], node_t *nd, int max); /* count or -1 */
void oc_select(const char *grp, const char *name);   /* async */
int  oc_cfgs(char cur[64], char names[][64], int max);           /* count */
void oc_cfg_switch(const char *name);      /* async, restarts openclash */

/* async jobs (fork + /tmp flag files); poll from UI tick */
void oc_job_svc_start(int on);             /* start/stop openclash */
void oc_job_conn_start(const char *group);  /* YT/GH latency check via Mihomo API */
int  oc_job_conn_poll(int *yt, int *gh);   /* 1 = fresh result */
void oc_job_test_start(const char *grp);   /* group delay test */
int  oc_job_test_poll(void);               /* 1 = just finished */
int  oc_test_apply(node_t *nd, int n_nd);  /* parse /tmp/mudi_test.json into delays */
int  oc_job_svc_poll(void);                /* 1 = svc job finished */

#endif
