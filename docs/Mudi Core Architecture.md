# Mudi 外屏插件系统 — Core 架构圣经

> **本文档是 Core 的最高设计与实现规范。后续任何模型/开发者修改 Core 前必读。**
> 配套文档：`Protocol v2.md`（契约）、`Mudi Plugin SDK v1.0.md`（插件侧）。

---

## 0. 项目目标

**Mudi 7 Linux 外屏插件系统** —— 在 GL.iNet Mudi 7 (GL-E5800) 的 2.8 寸外屏上，提供与官方 gl_screen 安全共存的第三方插件化 Dashboard。

已完成：
- fb0 接管（SIGSTOP/SIGCONT 冻结官方 UI，零损伤共存）
- touch（evdev 全链路：手势进入 / 点按 / 驱动层右滑退出）
- LVGL v8 渲染（CJK TTF 字体、深/浅主题）
- Core/Plugin IPC（进程隔离、管道文本协议）
- Protocol v2（声明式对象模型，插件零 LVGL）

**不可动摇的限制**：
1. **插件无 LVGL** —— 插件二进制中不得出现任何 LVGL 调用。
2. **Core 负责 UI** —— 所有渲染、布局、配色、导航只在 Core 内实现。

## 1. 系统总览

```
┌──────────── Mudi 7 (aarch64, Linux 5.15, OpenWrt 22.03) ────────────┐
│  gl_screen (官方UI, procd)  ←—— SIGSTOP/SIGCONT 共存 ——→  mudi-core  │
│                                                                     │
│  mudi-core 守护进程 (常驻): 读 /dev/input/event0, 等锁屏左缘右滑手势   │
│      │ 手势命中                                                      │
│      ├─ kill(gl_pid, SIGSTOP)   ← 冻结官方UI（绝不用 killall）        │
│      └─ fork() ──→ 会话子进程 (LVGL UI, 见 §3)                       │
│              │ fork+exec ──→ 插件进程A (openclash)                   │
│              │ fork+exec ──→ 插件进程B (zerotier, ...)               │
│              │ stdin/stdout 管道, Protocol v2                        │
│      会话结束(✕/右滑/锁屏键/超时)                                     │
│      └─ waitpid 回收 → kill(gl_pid, SIGCONT) → 官方UI无损恢复        │
└─────────────────────────────────────────────────────────────────────┘
```

**为什么 fork 会话而不是常驻 UI**：UI 状态（LVGL 对象树）每次会话全新构建，退出即整体回收，从机制上杜绝内存泄漏与状态腐烂；守护进程本身只有 60 行手势检测逻辑，永不出错。

## 2. 硬件参数（实测，勿改）

| 项 | 值 |
|---|---|
| 屏幕 | `/dev/fb0`，240×320 竖屏，16bpp RGB565，153600 字节 |
| 触摸 | `/dev/input/event0`，坐标范围 240×320，`EV_ABS ABS_MT_POSITION_X/Y` + `BTN_TOUCH` |
| 电源键 | `/dev/input/event1` (pmic_pwrkey)，`KEY_POWER` |
| 官方 UI | `pgrep gl_screen` 取 PID；`SIGSTOP` 冻结 / `SIGCONT` 恢复 |
| 进入手势 | 锁屏界面**左边缘 50px 内**按下，向右滑动 >50px，时长 <800ms |
| 电池 | `/sys/class/power_supply/cw221X-bat/capacity` |
| CPU | `/proc/stat` 两次采样差值（1s 间隔） |
| 内存 | `/proc/meminfo`（MemTotal − MemAvailable） |
| 网速 | `/sys/class/net/*/statistics` 收发字节差值（排除 lo） |

## 3. Core 内部结构

源码：`mudi-lvgl/`（交叉编译环境要求见 `DEPLOY.md` 构建章节）

| 文件 | 职责 |
|---|---|
| `main.c` | 守护进程：单实例锁、手势检测、SIGSTOP/fork 会话/SIGCONT、fb0 保存恢复 |
| `core_ui.c` | LVGL 全部：fb0 flush、触摸驱动、字体、主题、样式、状态栏、页面模板（卡片/详情/列表/设置/单选页）、导航栈、协议 v2 解析、BUSY 遮罩、自动锁屏 |
| `ipc.c` | 插件进程 fork+exec、管道、行缓冲读取、EOF 检测 |
| `registry.c` | 扫描 `/etc/mudi/plugins/*.json`，填充插件表，显隐配置读写 |
| `sys.c/h` | CPU/内存/电池/网速采集 + 四级变色工具 |
| `core.h` | 内部共享结构（plugin_t/manifest_t/pobj_t/plist_t），**插件永远看不到** |
| `libplugin.h/c` | 插件 SDK（编译进插件，不进 Core 逻辑） |
| `oc.c/h` + `plug_oc.c` | 第一个插件 OpenClash：数据层 + 协议层分离的参考实现 |

### Core 职责清单（边界：只有这些）

- 会话管理：手势进入、三路退出（✕ / 驱动层右滑 / 电源键）、自动锁屏
- 渲染：状态栏、主菜单卡片、详情页模板、列表页模板、设置页、单选子页
- 系统数据采集：1s 节拍统一采集 §2 数据源，IPC `TICK` 下发
- 插件生命周期：manifest 扫描、按显隐配置拉起/终止、管道 IO、协议解析
- 全局交互：BUSY 遮罩（插件 `BUSY 1` 触发，40s 兜底）、深/浅主题、四级变色
- 配置持久化：`/etc/mudi_ui.conf`（`light=`、`lock=`、`show_<id>=`）

### Core 永远不做

- 任何插件的业务语义（不知道什么是 OpenClash、什么是节点延迟）
- 任何插件数据源的访问（9090 API、uci、ZeroTier CLI 都是插件自己的事）
- 为某个插件定制页面（页面模板对全部插件统一）

## 4. 页面模板规范（UI v5.3 定稿，用户已确认）

### 4.1 顶栏（全局，56px）

- **第 1 行 状态行（20px，10px 字）**：`↑1.2K ↓0.8K C25% M62%` 一整串左对齐；右侧电池% + 电量条。C/M/电池按四级变色。
- **第 2 行 行动行（36px）**：
  - 主菜单：大 ⚙（左端）+ 大 ✕（右端），64×30 触控目标
  - 子页：`‹` 返回 + 标题（15px）+ 可选右侧按钮（如"测速"）
  - ⚙/✕ **只在主菜单出现**；插件页靠 `‹` 逐级返回或右滑退出

### 4.2 主菜单插件卡片（76px，只读）

整卡可点进详情；卡上**无可操作控件**。
- 第 1 行：名称 + ON/OFF 徽章（ON 白底黑字 / OFF 灰框）
- 第 2 行：`CF r2`（如 `Rule · hneko`；OFF 态显示"已停止"）
- 第 3 行：`CF r3l` + `CF r3r`

### 4.3 详情页模板（声明顺序即渲染顺序）

`OBJ switch/info/menu` 的声明顺序从上到下排布；menu 行绑定同 id list 自动获得 `›` 导航。单选子页统一：✓ + 行底高亮，点选即生效并自动返回。

### 4.4 颜色纪律（全系统仅这三处用色）

- 延迟：`<200` 绿 / `<500` 黄 / `<1000` 橙 / `≥1000` 红（metacubexd 风格字色）
- CPU/RAM：`<30%` 绿 / `<60%` 黄 / `<85%` 橙 / `≥85%` 红
- 电池：`>60%` 绿 / `>30%` 黄 / `>15%` 橙 / `≤15%` 红（电量条填充同步）
- 色值：绿 `#3FB950` 黄 `#E3B341` 橙 `#F0883E` 红 `#F85149`；**其余一切元素黑白灰**

### 4.5 自动锁屏

Core 会话层实现：设置页选项 关闭/15/30/60秒/5分钟，默认 30s（`conf lock=`）。
无输入超时 → 会话退出回官方锁屏。**BUSY 期间不计入 idle**（防止长操作被锁屏打断留下残留任务）。

## 5. 血泪纪律（每一条都出过事故，违反必翻车）

1. **LVGL tick 必须墙钟**。`LV_TICK_CUSTOM_SYS_TIME_EXPR` 用 `mudi_tick_ms()`（CLOCK_MONOTONIC 毫秒）。用 `clock()` 是 CPU 时间，LVGL 内部时间不走，短按被采样错过——点击偶发失灵的根因。
2. **管道 IPC 必须 `signal(SIGPIPE, SIG_IGN)`**（守护进程与会话子进程都要）。插件死亡后 Core 写管道触发 SIGPIPE 杀 Core，gl_screen 已 SIGSTOP，屏幕冻结在最后一帧。
3. **退出/返回右滑在触摸驱动层检测**，不依赖 LVGL 手势事件（手势只发给被按中的可点击对象，卡片上滑动手势永远到不了页面）。
4. **改 `lv_conf.h` 必须删对应 `.o` 再 make**（Makefile 无头文件依赖），否则配置不生效且极难排查。
5. **default_cn_medium.ttf 的 line_height = 2.0 倍字号**，全部文本天然下沉一倍字号。加载后必须手动收紧：`F10/12/13/15 → line_height=13/15/16/18, base_line=3`（实测字形边界，见 ftprobe2）。
6. **判断进程用 `pgrep -x`**；`pgrep -f` 会匹配发起命令的 shell 自身，误判进程存活。
7. **部署前先停服务再覆盖二进制**（`text file busy`），顺序：`/etc/init.d/mudi-core stop` → scp → chmod +x → `start`。
8. **插件 stdout 是协议通道**，调试输出只能去 stderr/文件。
9. **绝不用 `killall gl_screen`**（procd respawn 不可控）；只有 SIGSTOP/SIGCONT。

## 6. 文件与部署

| 路径 | 内容 |
|---|---|
| `/usr/bin/mudi-core` | Core 二进制（~918K，静态链接 LVGL 8.3 + FreeType） |
| `/usr/lib/mudi/plugins/<name>` | 插件二进制（独立进程，几 KB～几十 KB） |
| `/etc/mudi/plugins/<name>.json` | manifest：`id/name/exec/version/api/permissions` |
| `/etc/mudi_ui.conf` | `light=0|1`、`lock=<秒>`、`show_<id>=0|1` |
| `/etc/init.d/mudi-core` | procd 脚本，已 enable（`/etc/rc.d/S99mudi-core`） |

**插件安装/卸载 = 拷/删两个文件**，Core 零改动；设置页自动列出已安装插件做显隐（未安装不显示）。

## 7. 构建与调试

```sh
# 构建机 (任意 Linux x86_64, 装有 OpenWrt SDK)
cd ~/mudi-lvgl && make            # 产出 mudi-core + 三个插件
# SDK: OpenWrt 22.03.5 armvirt-64, aarch64_cortex-a53, musl, gcc 11.2
# FreeType/zlib 已静态编译进 SDK staging_dir (Makefile 顶部 SDK_DIR 按需修改)

# 设备 (OpenWrt 路由, root)
/etc/init.d/mudi-core stop && scp 覆盖 && chmod +x && /etc/init.d/mudi-core start
```

**调试工具**：
- `/tmp/inject`：evdev 注入器，`inject`（手势）/ `inject tap x y`，无需手指即可全链路自动化验证
- fb0 取证：`cat /dev/fb0 > /tmp/fb.raw`，拉到 PC 用 PIL 按 RGB565 转 PNG，逐像素核对渲染
- `/tmp/mudi-core.log`：Core 调试输出（启动时重定向）

**标准验证流程**：inject 手势 → tap 卡片 → 详情页 → 操作（遮罩）→ 结果态 → 右滑返回 → 右滑退出 → 确认 gl_screen 恢复、`ps` 无残留、`status=0` 干净退出。
