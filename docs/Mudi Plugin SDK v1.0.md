# Mudi Plugin SDK v1.0

> **插件开发者唯一需要读的文件。**
> 你写的是一个"提供状态与动作能力的服务"，不是一个"会画东西的程序"。
> 你看不到、也不需要看到 LVGL —— 屏幕由 Core 全权渲染。

---

## 1. 什么是插件

- 一个**独立的可执行文件**（独立进程，崩溃不连坐 Core）
- 通过 **stdin/stdout 文本协议（Protocol v2）** 与 Core 对话
- 声明自己有什么：开关、状态、菜单、列表；响应用户的点击；执行动作；汇报结果
- **安装 = 拷两个文件**：二进制 → `/usr/lib/mudi/plugins/<id>`，manifest → `/etc/mudi/plugins/<id>.json`。删除即卸载。

**红线（违反即不合格插件）**：
1. 不得 include 任何 LVGL 头文件，不得调用任何 LVGL 函数
2. 不得打开 `/dev/fb0`、`/dev/input/*`
3. `stdout` 只写协议行（用 libplugin 发送函数）；日志写 `stderr` 或 `/tmp/<id>.log`
4. 协议字段值中不得出现 TAB 与换行
5. 必须 `signal(SIGPIPE, SIG_IGN)`（libplugin 使用方默认要做）

## 2. 五分钟上手

```c
#include "libplugin.h"          /* 只有这一个头文件，链接 libplugin.c */
#include <signal.h>

static int on = 0;

static void on_init(void){                       /* 连接建立: 推入口卡片 */
    mudi_card(on);
    mudi_cf("r2", on ? "运行中" : "已停止", 's');
}

static void on_open(void){                       /* 用户进入详情页: 声明页面 */
    mudi_switch("svc", "服务", on);              /* 开关行 */
    mudi_info("run", "状态", on ? "运行中" : "已停止", on ? 'g' : 'd');
    mudi_menu("mode", "模式", "Rule");           /* 菜单行(绑定同名列表) */
    mudi_list("mode", "模式", NULL);             /* 列表声明 */
    mudi_item("mode", "rule",   "Rule",   "", 'n', 1);   /* sel=1 当前项 */
    mudi_item("mode", "global", "Global", "", 'n', 0);
    mudi_lend("mode");                           /* 批量发完必须 LEND */
}

static void on_ev(const char *id, const char *arg){
    if(!strcmp(id, "svc")){                      /* 开关: arg="0"/"1" */
        on = (arg[0] == '1');
        mudi_busy(1);                            /* 长操作: 阻塞遮罩 */
        /* ...fork 后台任务执行启停... 完成后: */
        mudi_card(on);
        mudi_set("run", on ? "运行中" : "已停止", on ? 'g' : 'd');
        mudi_busy(0);
    } else if(!strcmp(id, "mode")){              /* 列表项: arg=iid */
        mudi_set("mode/rule", "", 'n');          /* 旧项 checked=0 也可直接 */
        /* printf 风格 SET 由 SET ... checked= 完成，见 §4 */
        mudi_set("mode", (char*)arg, 'n');       /* 更新菜单行当前值 */
    }
}

static void on_tick(int sec){                    /* 每秒: 轮询/同步状态 */
    (void)sec;
}

int main(void){
    signal(SIGPIPE, SIG_IGN);
    mudi_plugin_t p = {
        .name = "MyPlugin",
        .on_init = on_init, .on_open = on_open,
        .on_ev = on_ev, .on_tick = on_tick,
    };
    return mudi_run(&p);                         /* 阻塞至 Core 发 EXIT */
}
```

编译：与 `libplugin.c` 一起静态编译即可，依赖只有 libc。

## 3. 生命周期回调

| 回调 | Core 消息 | 语义与纪律 |
|---|---|---|
| `on_init` | `INIT` | 连接建立后第一个回调。**采集初始状态，推 `CARD`/`CF`**。此时详情页不存在，不要发 OBJ |
| `on_open` | `OPEN` | 用户进入详情页。**必须幂等地完整声明全部 OBJ/list/item**——CLOSE 后 Core 已销毁全部对象，不能假设上次声明还在 |
| `on_ev(id,arg)` | `EV` | 用户交互。语义见 §5 |
| `on_tick(sec)` | `TICK` | 每秒一次（sec 从 0 递增）。轮询数据源、刷新卡片/页面值 |
| `on_close` | `CLOSE` | 用户离开详情页。释放页面临时缓存（可选） |
| （退出） | `EXIT` | `mudi_run` 返回，自行 `exit(0)`。随时可能发生（退出/锁屏/电源键） |

**状态自持有**：业务状态（当前模式、节点列表缓存、busy 标志…）全部存在插件自己进程里。Core 只保存渲染所需的最小副本，重启会话即丢。

## 4. API 参考（libplugin.h）

### 入口卡片

```c
void mudi_card(int on);                          // CARD ON/OFF 徽章
void mudi_cf(const char *row, const char *text, char color);
// row: "r2"=第2行, "r3l"=第3行左, "r3r"=第3行右
```

### 详情页对象声明（在 on_open）

```c
void mudi_switch(const char *id, const char *label, int on);
void mudi_info(const char *id, const char *label, const char *value, char color);
void mudi_menu(const char *id, const char *label, const char *value);
void mudi_list(const char *id, const char *title, const char *btn);   // btn 可为 NULL
void mudi_item(const char *list, const char *iid, const char *label,
               const char *sub, char color, int sel);
void mudi_lend(const char *list);   // 一批 item 发完必须调用, 否则不渲染
```

**绑定规则**：`mudi_menu` 与 `mudi_list` 用**同一个 id**，菜单行点击自动进入列表页；不绑定的 menu 行点击收到 `EV <id> "tap"`。

### 运行期更新

```c
void mudi_set(const char *id, const char *value, char color);  // 更新值+颜色
void mudi_checked(const char *id, int on);                     // 更新开关/选中态
// 列表项更新: id 用 "list/iid" 形式, 如 mudi_set("nodes/香港 01", "35ms", 'g')
void mudi_busy(int on);   // 1=全屏阻塞遮罩(spinner), 0=解除
```

### 颜色词（唯一允许的颜色表达）

`n` 主文本 · `s` 次要 · `d` 暗淡 · `f` 极淡 · `g` 绿 · `y` 黄 · `o` 橙 · `r` 红
分级语义：延迟 <200/<500/<1000/≥1000 → g/y/o/r；占用 <30/<60/<85/≥85% → g/y/o/r；电池 >60/>30/>15/≤15% → g/y/o/r。具体色值与主题由 Core 决定。

## 5. 事件语义（on_ev 的 id/arg）

| 来源 | id | arg | 说明 |
|---|---|---|---|
| switch 行 | 开关 id | `"0"` / `"1"` | 用户拨动后的新状态 |
| menu 行（无绑定列表） | 菜单 id | `"tap"` | 如"重启"按钮行 |
| list 项 | 列表 id | 项目 `iid` | Core 已自动返回详情页；切选中态用 `SET list/iid checked=` |
| list 头部按钮 | 列表 id | `"@btn"` | 如节点列表的"测速" |

## 6. 异步操作规范（转圈策略）

**凡是需要等待返回结果的操作，先 `mudi_busy(1)`，后台执行，完成后刷新数据并 `mudi_busy(0)`。**

| 操作类型 | 典型耗时 | 遮罩 |
|---|---|---|
| 服务启停 / 重启 / 切换配置（需重启进程） | 5~25s | ✅ 必须 |
| 全组测速 | 2~15s | ✅ 必须 |
| 切换节点 / 切换模式（API 即时返回） | <0.5s | ❌ 不要（亚秒级，遮罩反成干扰） |

实现模式（参考 plug_oc.c）：

```c
/* on_ev: */  busy=1; mudi_busy(1); unlink("/tmp/myjob.done"); fork_exec_job();
/* on_tick: */ if(busy && access("/tmp/myjob.done",F_OK)==0){ busy=0; mudi_busy(0);
                 refresh_all(); }      /* 完成文件轮询, 超时则主动重新采集状态兜底 */
```

- 后台任务用"完成文件"（`/tmp/<job>.done`）做信号，on_tick 里轮询，**绝不在 on_ev 里 sleep 阻塞**（会冻住整个协议循环）。
- Core 有 40 秒兜底自动解除遮罩；插件仍应自行管理超时并**主动刷新获取真实结果**，不能把兜底当正常路径。

## 7. manifest.json

```json
{
  "id": "openclash",
  "name": "OpenClash",
  "exec": "/usr/lib/mudi/plugins/openclash",
  "version": "1.0.0",
  "api": 2,
  "permissions": ["uci", "network", "service"]
}
```

- `id`：与二进制文件名一致，也是 `show_<id>=` 配置键
- `api`：协议版本，当前 `2`
- `permissions`：声明式清单（当前存档用，Core 后续据此做执行控制）

安装后用户在 **设置 → 插件** 看到显隐开关（只列出实际安装的插件）。

## 8. 参考实现：OpenClash 插件

`plug_oc.c`（协议层）+ `oc.c`（数据层）**分离**——这是推荐结构：

- **oc.c**：纯数据采集与动作（Clash REST API `127.0.0.1:9090`、uci、init.d），不知道协议存在
- **plug_oc.c**：只维护状态机 + 调 libplugin，把数据层结果翻译成对象声明/更新

覆盖能力：服务开关（遮罩）、运行状态、YT/GH 连通性（TICK 5s 轮询）、模式单选列表、配置单选列表（切换=重启+遮罩）、节点列表（64 项上限内全量、延迟四级变色、头部"测速"按钮）、点选切换。

**已踩过的坑（新插件别再踩）**：
- API 启动初期数据未就绪 → on_tick 里每 3s 重试直到成功
- mihomo JSON 键按字母序，`now`/`type` 在 `all` 之后，解析窗口必须覆盖整个对象
- 主代理组 = 成员最多的 Selector（排除 GLOBAL），别取"第一个"（有 bing/onedrive 干扰组）

## 9. 自查清单（交付前）

- [ ] 二进制中 `grep -c lv_` 为 0（无 LVGL 符号）
- [ ] stdout 除 libplugin 输出外无任何 printf
- [ ] on_open 重复调用两次结果一致（幂等）
- [ ] 所有长操作有遮罩 + 超时兜底 + 完成后刷新
- [ ] 杀掉插件进程，Core 与其他插件不受影响
- [ ] 删除二进制+json 后系统无残留引用
- [ ] **push_* 完整性规则**: 每个重建页面的 push 函数必须推送该页面上所有可能的 widget；
  - 不同状态分支(ON/OFF)须推送相同集合的 widget，用占位符("--"/"")填缺
  - 禁止条件性跳过 widget 推送（如 `if(cond) push() else 跳过`），必须总走 `mudi_set/mudi_cf` 包括 else 分支
  - 禁止提前 return 导致部分 widget 残留
