#!/bin/sh
# mudi-dev-install — 开发模式: 直接覆盖二进制, 无 ipk, 快速迭代
# 用法: scp mudi-core openclash zerotier cloudflared root@<device-ip>:/tmp/
#       ssh root@<device-ip> "sh /tmp/mudi-dev-install.sh"

set -e

echo "=== mudi-dev-install ==="

# 1. 停服务
killall -9 mudi-core 2>/dev/null || true
sleep 1
rm -f /tmp/mudi-core.lock

# 2. 恢复 gl_screen（如果有旧的 frozen 状态）
kill -CONT $(pidof gl_screen) 2>/dev/null || true

# 3. 安装二进制
cp /tmp/mudi-core /usr/bin/mudi-core 2>/dev/null || true
cp /tmp/openclash /usr/lib/mudi/plugins/openclash 2>/dev/null || true
cp /tmp/zerotier /usr/lib/mudi/plugins/zerotier 2>/dev/null || true
cp /tmp/cloudflared /usr/lib/mudi/plugins/cloudflared 2>/dev/null || true

chmod +x /usr/bin/mudi-core /usr/lib/mudi/plugins/*

# 4. 确保目录和manifest存在
mkdir -p /etc/mudi/plugins /usr/lib/mudi/plugins

# 5. 零 conf: 公开版插件不读 conf, 连接参数运行时自动从本体 UCI/数据目录发现

# 6. 启动
/usr/bin/mudi-core > /tmp/mudi-core.log 2>&1 &
sleep 1

echo "mudi-core: $(ps | grep -c '[m]udi-core') instance(s)"
echo "OK"
