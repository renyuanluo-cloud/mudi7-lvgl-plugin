/*
 * libplugin.h — Mudi Dashboard 插件 SDK（协议 v2 客户端）
 *
 * 插件 = 提供状态与动作能力的独立进程，不接触任何 UI/LVGL。
 * Core 通过 stdin/stdout 文本协议与本库通信，全部渲染由 Core 完成。
 *
 * 生命周期:
 *   main() {
 *       mudi_plugin_t p = { .name="OpenClash", .on_open=..., .on_ev=..., .on_tick=... };
 *       mudi_run(&p);   // 阻塞直到 Core 发送 EXIT
 *   }
 *
 * 页面声明（在 on_open 中调用）:
 *   mudi_switch("svc", "服务", 1);            // 开关行
 *   mudi_info("run", "状态", "运行中", 'g');   // 信息行
 *   mudi_menu("mode", "模式", "Rule");         // 菜单行(可关联列表)
 *   mudi_list("mode", "模式", NULL);           // 声明列表
 *   mudi_item("mode", "rule", "Rule", "", 'n', 1);  // 列表项(选中)
 *
 * 运行期更新:
 *   mudi_set("mode", "Global", 'n');           // 更新任意对象的值+颜色
 *   mudi_checked("svc", 0);                    // 更新开关状态
 *   mudi_card(1);                              // 更新入口卡片徽章 ON/OFF
 *   mudi_cf("r2", "Rule · hneko", 's');        // 卡片第2行 / 第3行左 / 第3行右
 *
 * 颜色: n=主文本 s=次要 d=暗淡 f=极淡 g=绿 y=黄 o=橙 r=红
 *
 * 事件 (on_ev):
 *   switch 行: id=开关id, arg="0"/"1"
 *   menu 行(无列表): id=菜单id, arg="tap"
 *   list 项: id=列表id, arg=项目iid
 *   list 头部按钮: id=列表id, arg="@btn"
 */
#ifndef MUDI_LIBPLUGIN_H
#define MUDI_LIBPLUGIN_H

#define MUDI_API_VER 2

typedef struct {
    const char *name;                          /* 显示名(标题栏) */
    void (*on_init)(void);                     /* 连接建立: 推入口卡片数据 (mudi_card/mudi_cf) */
    void (*on_open)(void);                     /* 用户打开详情页: 在此声明页面对象 */
    void (*on_ev)(const char *id, const char *arg);   /* 用户交互事件 */
    void (*on_tick)(int sec);                  /* 每秒节拍(sec 从 0 递增) */
    void (*on_close)(void);                    /* 用户离开详情页 */
} mudi_plugin_t;

int  mudi_run(mudi_plugin_t *p);               /* 握手并进入事件循环 */

/* 发送辅助 (printf 到 stdout, Core 解析) */
void mudi_card(int on);
void mudi_cf(const char *row, const char *text, char color);   /* row: r2|r3l|r3r */
void mudi_switch(const char *id, const char *label, int on);
void mudi_info(const char *id, const char *label, const char *value, char color);
void mudi_menu(const char *id, const char *label, const char *value);
void mudi_list(const char *id, const char *title, const char *btn, const char *btn2);
void mudi_item(const char *list, const char *iid, const char *label,
               const char *sub, char color, int sel);
void mudi_lend(const char *list);   /* 列表项发送完毕, 通知 Core 渲染/刷新列表页 */
void mudi_set(const char *id, const char *value, char color);
void mudi_checked(const char *id, int on);
void mudi_busy(int on);   /* 1=显示阻塞遮罩(Core), 0=解除 */

/* ---- 插件配置 (可选遗留接口) ----
 * 公开版三插件均不使用本接口: 连接参数运行时自动从本体 UCI/数据目录读取, 零 conf。
 * 本函数保留供需要私有拓扑配置的插件可选使用 (如自用版 zerotier moon 同步拓扑)。
 * 配置存于 /etc/mudi/plugins/<id>.conf, id = 插件可执行文件名 (不含路径)。
 * 格式: 每行 KEY=VALUE, '#' 开头为注释, 空行忽略。
 * conf 文件不存在或键缺失 → 写入 dflt 并返回 0; 读到 → 返回 1。
 */
int mudi_conf_get(const char *key, const char *dflt, char *out, int sz);

#endif
