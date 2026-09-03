#!/bin/sh
# mudi-install — 生产安装: IPK 一键安装
# 用法: 把 ipk 文件放到 /tmp 下, 然后:
#       sh /tmp/mudi-install.sh
# 或:  opkg install /path/to/mudi-core_*.ipk /path/to/mudi-plugin-*.ipk

set -e

echo "=== Mudi 7 External Screen Plugin System ==="
echo "Installing from /tmp/*.ipk ..."

# 安装(依赖自动解析)
for ipk in /tmp/mudi-core*.ipk /tmp/mudi-plugin-*.ipk; do
    [ -f "$ipk" ] || continue
    echo "  $ipk"
    opkg install --force-overwrite "$ipk"
done

# 检查 LVGL 依赖
if ! opkg list-installed | grep -q libfreetype; then
    echo "ERROR: libfreetype not installed. LVGL requires freetype for font rendering."
    exit 1
fi

# 启动服务
/etc/init.d/mudi-core restart 2>/dev/null || /usr/bin/mudi-core &

echo ""
echo "=== Installation Complete ==="
echo "Swipe left from edge to enter the dashboard."
echo "Plugins installed:"
for m in /etc/mudi/plugins/*.json; do
    [ -f "$m" ] && echo "  $(basename $m .json)"
done
echo ""
echo "零配置: 插件连接参数运行时自动从本体 UCI/数据目录发现, 无需 conf"
echo "部署指引: 见仓库 docs/DEPLOY.md (各插件系统层先决条件)"
echo ""
echo "Dev mode: scp binaries to /tmp/ and run /tmp/mudi-dev-install.sh"
