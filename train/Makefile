ALT = /ALT
P8 = $(ALT)/p8
Sisyphus = $(ALT)/Sisyphus
# Intermingle distro/arch lists due to memory limits.
LISTS = \
	$(Sisyphus)/x86_64/base/pkglist.classic.xz \
	$(Sisyphus)/x86_64/base/srclist.classic.xz \
	$(Sisyphus)/noarch/base/pkglist.classic.xz \
	$(Sisyphus)/i586/base/pkglist.debuginfo.xz \
	$(P8)/i586/base/pkglist.classic.xz \
	$(P8)/i586/base/srclist.classic.xz \
	$(P8)/noarch/base/pkglist.classic.xz \
	$(P8)/x86_64/base/pkglist.debuginfo.xz

RPM_OPT_FLAGS ?= -O2 -g -Wall
# zstd -> BUILD/zstd-1.1.4
ZSTD_ADD = -Izstd/lib/dictBuilder zstd/lib/libzstd.a

all: train rpmhdr.zstdict
train: train.c
	$(CC) $(RPM_OPT_FLAGS) $^ $(ZSTD_ADD) -o $@
rpmhdr.zstdict: train
	xzcat -dc $(LISTS) |./train >rpmhdr.zstdict
