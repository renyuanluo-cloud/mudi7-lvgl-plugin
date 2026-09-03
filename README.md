# Mudi 7 外屏插件系统

GL.iNet Mudi 7（及同系带屏 OpenWrt 设备）外置屏幕的插件化 UI 系统。
Core 接管小屏（LVGL），第三方应用以**独立进程插件**形式提供状态与动作，全部渲染由 Core 完成——插件零 UI 代码。

## 架构一览

```
┌─────────────────────────────────────────────┐
│  gl_screen (原生锁屏)  SIGSTOP/SIGCONT 协作   │
│  /dev/fb0   ──►  mudi-core (LVGL 全部 UI)    │
│                    ▲  stdin/stdout 协议 v2     │
│   ┌──────────┐  ┌──────────┐  ┌────────────┐ │
│   │ OpenClash │  │ ZeroTier │  │ Cloudflare │ │   ← 插件 = 独立进程
│   │ 插件       │  │ 插件      │  │ Tunnel 插件 │ │      状态 + 动作, 无 UI
│   └──────────┘  └──────────┘  └────────────┘ │
└─────────────────────────────────────────────┘
```

- **Core**（`mudi-core`）：手势、会话、fb0 驱动、LVGL 渲染、页面模板（卡片/详情/列表）、插件生命周期、协议解析
- **插件**（`/usr/lib/mudi/plugins/*`）：独立进程，通过协议 v2 推送状态 / 接收交互，不接触任何 UI
- **契约**：`docs/Protocol v2.md`；插件开发见 `docs/Mudi Plugin SDK v1.0.md`

## 仓库结构

| 路径 | 说明 |
|---|---|
| `main.c / core_ui.c / ipc.c / registry.c / sys.c` | Core 守护进程 |
| `libplugin.c/h` | 插件 SDK（含可选遗留配置读取接口，公开版插件不依赖 conf） |
| `plug_oc.c + oc.c/h` | OpenClash 插件（数据层与协议层分离的参考实现） |
| `plug_zt.c` | ZeroTier 插件 |
| `plug_cf.c` | Cloudflare Tunnel 插件 |
| `manifests/*.json` | 插件注册清单（Core 据此发现插件） |
| `packaging/` | IPK 打包目录与设备端安装脚本 |
| `install.sh` | dist tar 包一键安装（交互选择 + 双门禁） |
| `docs/` | 架构 / 协议 / SDK 规范 + 部署指引 |

## 快速开始

三插件均有系统层前置应用，**先装好再装本系统**（详见 `docs/DEPLOY.md`）：

| 插件 | 前置应用 | 系统层需先完成 |
|---|---|---|
| OpenClash | luci-app-openclash | 后台配置好订阅并启用 |
| ZeroTier | zerotier 包 | zerotier-cli join 加入网络 |
| Cloudflare Tunnel | cloudflared（官方包，自带 UCI） | cloudflared login + 建隧道/绑域名 |

### 方式 A：免编译 tar 包（构建机 `make dist` 后上传）

```sh
# 设备端:
tar xzf mudi-dist.tar.gz -C /tmp/mudi-dist
sh /tmp/mudi-dist/install.sh        # 交互选择插件 → 双门禁校验 → 装二进制/manifest → 启动
```

### 方式 B：IPK（构建机 `make ipk` 后 scp 到设备）

```sh
opkg install --force-overwrite /tmp/mudi-core*.ipk
opkg install --force-overwrite /tmp/mudi-plugin-*.ipk   # postinst 双门禁校验, 不过则拒绝安装
```

完成后：外屏**左边缘右滑**进入系统。

## 零配置（自动发现）

三个插件**不读取任何 conf**。API 端口、鉴权口令、二进制路径、ZeroTier 数据目录与版本号等连接参数，
全部运行时从本体自身的 UCI / 数据目录自动发现（`cn_port` / `dashboard_password` / `/etc/config/cloudflared` /
`/var/lib/zerotier-one` / `which` / `zerotier-cli info`）。部署者装好本体即可，无需改任何源码或填任何配置。

## 构建

需要 OpenWrt SDK（armvirt-64 / aarch64_cortex-a53 / musl / gcc 11.2），SDK 路径在 `Makefile` 顶部 `SDK_DIR` 调整。
另需 LVGL 8.3.11 第三方源码，clone 到仓库根目录的 `lvgl/` 子目录（可用 `make LVGL_DIR=/path` 覆盖路径）：

```sh
git clone -b v8.3.11 https://github.com/lvgl/lvgl.git lvgl
make            # mudi-core + openclash + zerotier + cloudflared
make dist       # 免编译部署包 mudi-dist.tar.gz
make ipk        # 四个 IPK
```

## 设计文档

- `docs/Mudi Core Architecture.md` — Core 内部结构与设计规范
- `docs/Protocol v2.md` — Core ↔ 插件唯一契约
- `docs/Mudi Plugin SDK v1.0.md` — 插件开发指南
- `docs/DEPLOY.md` — 部署指引（先决条件/配置/排查）
