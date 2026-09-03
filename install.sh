#!/bin/sh
# mudi-dist 安装脚本 — 在解包目录内执行
#   tar xzf mudi-dist.tar.gz -C /tmp/mudi-dist && sh /tmp/mudi-dist/install.sh
# 适用于 OpenWrt / GL.iNet 等 OpenWrt 系设备。
#
# 安装原则 (产品化):
#   - 交互选择要安装哪些外屏插件;
#   - 每个插件两道门禁: ①本体已装 ②运行且已配置; 任一不过 → 不装;
#   - 插件连接参数运行时自动从对应本体 (UCI/数据目录) 读取, 无需任何 conf 或手动输入;
#   - 只停 mudi-core, 绝不碰用户的代理/隧道/内网服务。
set -e
cd "$(dirname "$0")" || exit 1   # 切到解包目录, 相对路径才有效

echo "=== Mudi 外屏插件系统 安装 ==="
echo ""

# ---------- 门禁检查函数 (①本体已装 ②运行且已配置) ----------
gate_openclash(){
    if [ ! -x /etc/init.d/openclash ]; then
        echo "  ① 未安装 OpenClash (缺 /etc/init.d/openclash)"
        return 1
    fi
    local en port cfg
    en=$(uci -q get openclash.config.enable 2>/dev/null)
    port=$(uci -q get openclash.config.cn_port 2>/dev/null); [ -n "$port" ] || port=9090
    cfg=$(uci -q get openclash.config.config_path 2>/dev/null)
    if [ "$en" != "1" ]; then
        echo "  ② OpenClash 未启用 (enable != 1)"
        return 1
    fi
    if [ -n "$cfg" ] && [ ! -f "$cfg" ]; then
        echo "  ② OpenClash 配置文件不存在 ($cfg)"
        return 1
    fi
    if ! curl -s -m 3 "http://127.0.0.1:${port}/version" 2>/dev/null | grep -q version; then
        echo "  ② OpenClash 内核未运行 (127.0.0.1:${port}/version 不通)"
        return 1
    fi
    return 0
}

gate_zerotier(){
    if [ ! -x /etc/init.d/zerotier ]; then
        echo "  ① 未安装 ZeroTier (缺 /etc/init.d/zerotier)"
        return 1
    fi
    if [ ! -f /var/lib/zerotier-one/zerotier-one.port ]; then
        echo "  ② ZeroTier 未运行 (缺 zerotier-one.port)"
        return 1
    fi
    if ! ls /var/lib/zerotier-one/networks.d/*.conf >/dev/null 2>&1; then
        echo "  ② ZeroTier 未加入任何网络 (networks.d 为空)"
        return 1
    fi
    return 0
}

gate_cloudflared(){
    if [ ! -f /etc/config/cloudflared ] || [ ! -x /etc/init.d/cloudflared ]; then
        echo "  ① 未安装 cloudflared (缺 /etc/config/cloudflared 或 /etc/init.d/cloudflared)"
        return 1
    fi
    local en token
    en=$(uci -q get cloudflared.config.enabled 2>/dev/null)
    token=$(uci -q get cloudflared.config.token 2>/dev/null)
    if [ "$en" != "1" ]; then
        echo "  ② cloudflared 未启用 (enabled != 1)"
        return 1
    fi
    if [ -z "$token" ]; then
        echo "  ② cloudflared 未配置 token"
        return 1
    fi
    return 0
}

# ---------- 交互选择要安装的插件 ----------
echo "请选择要安装的外屏插件 (空格分隔序号, 直接回车 = 全部):"
echo "  1) OpenClash"
echo "  2) ZeroTier"
echo "  3) Cloudflare Tunnel"
printf "你的选择: "
read choice 2>/dev/null || choice=""

sel_oc=0; sel_zt=0; sel_cf=0
if [ -z "$choice" ]; then
    sel_oc=1; sel_zt=1; sel_cf=1
else
    for c in $choice; do
        case "$c" in
            1) sel_oc=1;;
            2) sel_zt=1;;
            3) sel_cf=1;;
        esac
    done
fi

# ---------- 逐个门禁 + 安装 ----------
mkdir -p /usr/lib/mudi/plugins /etc/mudi/plugins

install_one(){
    local name=$1 gate=$2 bin=$3 mf=$4
    echo ""
    echo "--- $name ---"
    if ! "$gate"; then
        echo "  ! 未通过检查, 跳过安装 (请先安装并配置好本体后重试)"
        return 1
    fi
    cp -f "$bin" /usr/lib/mudi/plugins/
    cp -f "$mf" /etc/mudi/plugins/
    echo "  OK 已安装"
    return 0
}

[ "$sel_oc" = 1 ] && install_one "OpenClash" gate_openclash plugins/openclash manifests/openclash.json || true
[ "$sel_zt" = 1 ] && install_one "ZeroTier" gate_zerotier plugins/zerotier manifests/zerotier.json || true
[ "$sel_cf" = 1 ] && install_one "Cloudflare Tunnel" gate_cloudflared plugins/cloudflared manifests/cloudflared.json || true
chmod +x /usr/lib/mudi/plugins/* 2>/dev/null || true

# ---------- 安装 mudi-core 本体 + 只停旧 mudi-core (绝不动用户服务) ----------
cp -f mudi-core /usr/bin/mudi-core
chmod +x /usr/bin/mudi-core
killall -9 mudi-core 2>/dev/null || true
GP=$(ps | grep gl_screen | grep -v grep | awk '{print $1}' | head -1)
[ -n "$GP" ] && kill -CONT $GP 2>/dev/null || true
sleep 1
rm -f /tmp/mudi-core.lock /tmp/mudi_svc.done /tmp/mudi_test.done /tmp/mudi_conn.done

# ---------- 启动 ----------
rm -f /tmp/mudi-core.lock
if [ -x /etc/init.d/mudi-core ]; then
    /etc/init.d/mudi-core start 2>/dev/null || /usr/bin/mudi-core >/tmp/mudi-core.log 2>&1 &
else
    /usr/bin/mudi-core >/tmp/mudi-core.log 2>&1 &
fi
sleep 2

# ---------- 验证 ----------
echo ""
echo "mudi-core: $(ps | grep -c '[m]udi-core') instance(s)"
echo "已装插件: $(ls /usr/lib/mudi/plugins/ 2>/dev/null | tr '\n' ' ')"
echo "OK"
echo ""
echo "下一步: 左滑进入外屏查看各插件; 插件连接参数自动从对应本体读取, 无需手动填写。"
