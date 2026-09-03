SDK_DIR = $(HOME)/openwrt-sdk-22.03.5-armvirt-64_gcc-11.2.0_musl.Linux-x86_64
TC_DIR  = $(SDK_DIR)/staging_dir/toolchain-aarch64_cortex-a53_gcc-11.2.0_musl
TT_DIR  = $(SDK_DIR)/staging_dir/target-aarch64_cortex-a53_musl

CC    = $(TC_DIR)/bin/aarch64-openwrt-linux-musl-gcc
STRIP = $(TC_DIR)/bin/aarch64-openwrt-linux-musl-strip

CFLAGS  = -O2 -Wall -Wno-error -I. -D_GNU_SOURCE
CFLAGS += -I$(LVGL_DIR)
CFLAGS += -I$(TT_DIR)/usr/include/freetype2
CFLAGS += -DLV_CONF_INCLUDE_SIMPLE
CFLAGS += -DLV_USE_ANIMIMG=0 -DLV_USE_CALENDAR=0 -DLV_USE_CHART=0
CFLAGS += -DLV_USE_KEYBOARD=0 -DLV_USE_MSGBOX=0 -DLV_USE_SPAN=0
CFLAGS += -DLV_USE_SPINBOX=0 -DLV_USE_TABVIEW=0
CFLAGS += -DLV_USE_TILEVIEW=0 -DLV_USE_WIN=0 -DLV_USE_MENU=0
CFLAGS += -DLV_USE_LED=0 -DLV_USE_LIST=0 -DLV_USE_METER=0
CFLAGS += -DLV_USE_IMGBTN=0 -DLV_USE_COLORWHEEL=0
CFLAGS += -MMD -MP          # 头文件依赖跟踪: 改 core.h 等会自动全量重编受影响目标

PLUG_OC_SRC = plug_oc.c oc.c libplugin.c
PLUG_ZT_SRC = plug_zt.c libplugin.c
PLUG_CF_SRC = plug_cf.c libplugin.c
PLUG_OC_OBJ = $(PLUG_OC_SRC:.c=.o)
PLUG_ZT_OBJ = $(PLUG_ZT_SRC:.c=.o)
PLUG_CF_OBJ = $(PLUG_CF_SRC:.c=.o)

ALL_OBJ = $(LVGL_OBJ) $(CORE_OBJ) $(PLUG_OC_OBJ) $(PLUG_ZT_OBJ) $(PLUG_CF_OBJ)
DEPS = $(ALL_OBJ:.o=.d)
-include $(DEPS)

# LVGL 第三方源码: 默认取仓库根目录下的 lvgl/（clone v8.3.11 到此）, 可用 `make LVGL_DIR=/path` 覆盖
LVGL_DIR ?= $(CURDIR)/lvgl
LVGL_SRC_DIR = $(LVGL_DIR)/src
LVGL_SRC  = $(wildcard $(LVGL_SRC_DIR)/core/*.c)
LVGL_SRC += $(wildcard $(LVGL_SRC_DIR)/draw/*.c)
LVGL_SRC += $(wildcard $(LVGL_SRC_DIR)/draw/sw/*.c)
LVGL_SRC += $(LVGL_SRC_DIR)/extra/lv_extra.c
LVGL_SRC += $(LVGL_SRC_DIR)/extra/layouts/flex/lv_flex.c
LVGL_SRC += $(LVGL_SRC_DIR)/extra/layouts/grid/lv_grid.c
LVGL_SRC += $(LVGL_SRC_DIR)/extra/libs/freetype/lv_freetype.c
LVGL_SRC += $(LVGL_SRC_DIR)/extra/widgets/spinner/lv_spinner.c
LVGL_SRC += $(wildcard $(LVGL_SRC_DIR)/font/*.c)
LVGL_SRC += $(wildcard $(LVGL_SRC_DIR)/hal/*.c)
LVGL_SRC += $(wildcard $(LVGL_SRC_DIR)/misc/*.c)
LVGL_SRC += $(wildcard $(LVGL_SRC_DIR)/widgets/*.c)
LVGL_OBJ = $(LVGL_SRC:.c=.o)

CORE_SRC = main.c sys.c core_ui.c ipc.c registry.c
CORE_OBJ = $(CORE_SRC:.c=.o)

all: mudi-core openclash zerotier cloudflared

mudi-core: $(LVGL_OBJ) $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -static -L$(TT_DIR)/usr/lib -lfreetype -lz
	$(STRIP) --strip-all $@
	@ls -lh $@

openclash: plug_oc.o oc.o libplugin.o
	$(CC) $(CFLAGS) -o $@ $^ -static
	$(STRIP) --strip-all $@
	@ls -lh $@

zerotier: plug_zt.o libplugin.o
	$(CC) $(CFLAGS) -o $@ $^ -static
	$(STRIP) --strip-all $@
	@ls -lh $@

cloudflared: plug_cf.o libplugin.o
	$(CC) $(CFLAGS) -o $@ $^ -static
	$(STRIP) --strip-all $@
	@ls -lh $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(ALL_OBJ) $(DEPS) ui.o mudi-core openclash zerotier cloudflared
	rm -rf dist mudi-dist.tar.gz

# ===== 快速部署包 (tar.gz, 免 opkg) =====
dist: all
	rm -rf dist && mkdir -p dist/plugins dist/manifests
	cp mudi-core dist/
	cp openclash zerotier cloudflared dist/plugins/
	cp manifests/*.json dist/manifests/ 2>/dev/null || true
	cp install.sh dist/ 2>/dev/null || true
	chmod +x dist/install.sh 2>/dev/null || true
	tar czf mudi-dist.tar.gz -C dist .
	@echo "=== 部署包: mudi-dist.tar.gz ==="
	@ls -lh mudi-dist.tar.gz
	@echo "上传到目标机后执行: tar xzf mudi-dist.tar.gz -C /tmp/mudi-dist && sh /tmp/mudi-dist/install.sh"

# ===== IPK 打包 =====
SDK_DIR := $(HOME)/openwrt-sdk-22.03.5-armvirt-64_gcc-11.2.0_musl.Linux-x86_64
OPKG_BUILD := PATH=$(SDK_DIR)/staging_dir/host/bin:$$PATH $(SDK_DIR)/scripts/ipkg-build

PKG := packaging
DIST := $(HOME)/dist

ipk: mudi-core openclash zerotier cloudflared
	mkdir -p $(DIST)
	# fix permissions lost in tar transfer
	chmod +x $(PKG)/mudi-core/CONTROL/postinst $(PKG)/mudi-core/CONTROL/prerm $(PKG)/mudi-core/etc/init.d/mudi-core 2>/dev/null; true
	# copy binaries
	cp mudi-core $(PKG)/mudi-core/usr/bin/
	cp openclash $(PKG)/mudi-plugin-openclash/usr/lib/mudi/plugins/
	cp zerotier $(PKG)/mudi-plugin-zerotier/usr/lib/mudi/plugins/
	cp cloudflared $(PKG)/mudi-plugin-cloudflared/usr/lib/mudi/plugins/
	# build ipks (no per-plugin conf: 连接参数运行时自动从本体 UCI/数据目录读取)
	$(OPKG_BUILD) $(PKG)/mudi-core $(DIST)
	$(OPKG_BUILD) $(PKG)/mudi-plugin-openclash $(DIST)
	$(OPKG_BUILD) $(PKG)/mudi-plugin-zerotier $(DIST)
	$(OPKG_BUILD) $(PKG)/mudi-plugin-cloudflared $(DIST)
	@echo "=== IPK packages in $(DIST) ==="
	@ls -lh $(DIST)/*.ipk
	@echo ""
	@echo "Install: opkg install --force-overwrite $(DIST)/mudi-core*.ipk"
	@echo "         opkg install --force-overwrite $(DIST)/mudi-plugin-*.ipk"
	@echo ""
	@echo "插件连接参数运行时自动从本体读取, 无需任何 conf 或手动输入"

.PHONY: all clean ipk dist
