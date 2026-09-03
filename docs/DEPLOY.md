# 部署指引（DEPLOY）

本系统 = Mudi 7（GL-E5800 等带小屏 OpenWrt 设备）外屏上的**状态与快捷操作面板**。
它不替代任何应用的管理后台——三个插件的"本体"（OpenClash / ZeroTier / cloudflared）都必须在系统层先装好、先配置好，外屏插件只负责展示状态 + 提供常用动作。

**核心原则（产品化）**：插件的连接参数（API 端口、鉴权口令、二进制路径、ZeroTier 数据目录、版本号）全部**运行时从本体自身的 UCI / 数据目录自动发现**，不需要任何 conf，也不需要任何手动输入。

---

## 1. 环境要求

| 项 | 要求 |
|---|---|
| 设备 | GL.iNet Mudi 7（或同架构带屏 OpenWrt 设备） |
| CPU 架构 | aarch64_cortex-a53（预编译二进制绑定此架构） |
| 系统 | OpenWrt 22.03 系 / GL 官方固件（需 gl_screen 原生锁屏协作） |
| 小屏 | /dev/fb0 RGB565（240×320），触摸输入事件 |
| 依赖 | libfreetype（LVGL 字体渲染，装 mudi-core 前先 `opkg install libfreetype`） |

> 非 GL 固件若没有 gl_screen，Core 仍可运行（SIGSTOP 协作自动跳过），
> 但需自行确保无其它进程抢占 fb0。

---

## 2. 前置应用安装与系统层配置（按插件）

### 2.1 ZeroTier

```sh
opkg update && opkg install zerotier
/etc/init.d/zerotier enable && /etc/init.d/zerotier start
zerotier-cli join <16位网络ID>
```

插件自动读取 `/var/lib/zerotier-one/`（identity.secret / zerotier-one.port / networks.d/*.conf），
外屏显示：节点 ID、版本（`zerotier-cli info` 动态读）、当前 IP、网络列表、以及链路状态。

**链路状态（"模式"行）**：插件读 `zerotier-cli peers` 全部行，取整体链路最优值——
存在 DIRECT 对端即显示"直连"，仅有 RELAY 即显示"中继"，二者皆无显示"未连接"。
这不依赖也不要求配置 moon，语义就是当前节点间是直连还是中继。

### 2.2 OpenClash

安装 luci-app-openclash（ipk / 官方 release 覆盖安装均可），进 OpenClash 后台：
1. 配置好订阅并启用（`enable=1`）
2. 保持外部控制器 (External Controller) 默认端口（`cn_port`，默认 `9090`）——插件自动读该 UCI 键，无需手动同步
3. 如需鉴权，插件自动读 `dashboard_password`；未设则按无鉴权模式请求

插件自动从 UCI 读 `cn_port` / `dashboard_password` / `config_path`；探测复用 mihomo API
（YouTube / GitHub 两个固定可达性目标为功能常量）；订阅更新脚本路径 `which openclash_update.sh` 自动探测。

### 2.3 Cloudflare Tunnel

安装 OpenWrt **官方 cloudflared 包**（自带 UCI `/etc/config/cloudflared`，键 `enabled` / `token`）：

```sh
opkg update && opkg install cloudflared   # 官方包自带 UCI, 无需 luci-app-cloudflared
```

隧道本体请在 LuCI → 服务 → Cloudflare 完成：`cloudflared login` → 创建隧道 → 绑定域名/服务。
本插件只负责隧道进程的启停与连接状态显示（二进制路径 `which cloudflared` 自动探测）。

---

## 3. 安装本系统

### 方式 A：免编译 tar 包

```sh
# 构建机: make dist  → 得到 mudi-dist.tar.gz → scp 到设备 /tmp/
tar xzf /tmp/mudi-dist.tar.gz -C /tmp/mudi-dist
sh /tmp/mudi-dist/install.sh
```

脚本流程：**交互选择要安装的插件**（OpenClash / ZeroTier / Cloudflare Tunnel，空格分隔序号，回车全选）→
对每个选中的插件执行两道门禁（①本体已装 ②运行且已配置）→ 任一不过则跳过该插件 →
只停旧 mudi-core（绝不动用户的代理/隧道/内网服务）→ 安装二进制与 manifest → 启动并验证。

### 方式 B：IPK

```sh
opkg install --force-overwrite /tmp/mudi-core_*.ipk
opkg install --force-overwrite /tmp/mudi-plugin-openclash_*.ipk
opkg install --force-overwrite /tmp/mudi-plugin-zerotier_*.ipk
opkg install --force-overwrite /tmp/mudi-plugin-cloudflared_*.ipk
/etc/init.d/mudi-core enable && /etc/init.d/mudi-core restart
```

> 每个插件的 IPK `postinst` 都会执行同样的双门禁；不满足则 `exit 1` 拒绝安装并打印原因，
> 且**不会自动替你安装本体**（本体应由你在系统层装好、配好）。

### 方式 C：开发迭代

见 `packaging/mudi-dev-install.sh`（scp 二进制到设备直接覆盖，不清配置）。

**验证**：外屏左边缘右滑进入面板 → 已装插件卡片出现，点开详情页查看状态。

---

## 4. 配置说明

**公开版零 conf**：三个插件不读取任何 `/etc/mudi/plugins/*.conf`，所有连接参数运行时自动发现：

| 插件 | 自动发现的参数 |
|---|---|
| OpenClash | `uci openclash.config.cn_port`（端口）、`dashboard_password`（鉴权）、`config_path`（当前配置）、`which openclash_update.sh`（订阅脚本） |
| ZeroTier | `/var/lib/zerotier-one/` 数据目录、`zerotier-cli info` 版本、`zerotier-cli peers` 链路状态 |
| Cloudflare Tunnel | `/etc/config/cloudflared`（enabled/token）、`which cloudflared` 二进制路径 |

---

## 5. 常见问题排查

| 现象 | 原因 / 处理 |
|---|---|
| 安装时某插件被跳过 | 门禁未过（未装本体 / 未启用 / 未运行 / 未配置）。先装好并配置好本体，再重装该插件 |
| OpenClash 卡片显示"已停止"，但后台明明在跑 | 到 `curl http://127.0.0.1:$(uci get openclash.config.cn_port)/version` 验证返回；端口异常通常是 OpenClash 未启用或内核未启动 |
| OpenClash 详情页节点列表为空 | OpenClash 启动初期 API 未就绪，插件每 3s 自动重试；仍空则确认订阅里有可用节点 |
| 订阅"全更新/更新"按钮不显示 | `which openclash_update.sh` 找不到脚本（非 ipk 安装或路径异常）——插件自动隐藏该按钮 |
| Cloudflare 卡片一直"已停止" | cloudflared 未启用或未配置 token；确认 `uci show cloudflared` |
| ZeroTier 详情"模式"行显示"未连接" | 正常——当前所有对端均未建立 DIRECT/RELAY 链路（可能未加入网络或网络无对端） |
| ZeroTier 版本显示 "--" | zerotier-cli 暂不可用（服务刚启动），会自动重取 |

---

## 6. 从源码构建

```sh
# Linux x86_64 + OpenWrt SDK 22.03.5 armvirt-64 (aarch64_cortex-a53, musl, gcc 11.2)
# 下载: https://downloads.openwrt.org/releases/22.03.5/targets/armvirt/64/
# 解压后把 SDK 根目录路径填进 Makefile 顶部 SDK_DIR

# LVGL 8.3.11 第三方源码 clone 到仓库根目录 lvgl/（可用 make LVGL_DIR=/path 覆盖）
git clone -b v8.3.11 https://github.com/lvgl/lvgl.git lvgl

make            # 产物: mudi-core openclash zerotier cloudflared (均为静态链接)
make dist       # mudi-dist.tar.gz 免编译部署包
make ipk        # 四个 .ipk
```
