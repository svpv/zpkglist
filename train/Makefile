ALT = /ALT
Stable = $(ALT)/p8
Sisyphus = $(ALT)/Sisyphus
# Intermingle distro/arch lists due to memory limits.
LISTS = \
	$(Sisyphus)/x86_64/base/pkglist.classic.xz \
	$(Sisyphus)/x86_64/base/srclist.classic.xz \
	$(Sisyphus)/noarch/base/pkglist.classic.xz \
	$(Sisyphus)/i586/base/pkglist.debuginfo.xz \
	$(Stable)/i586/base/pkglist.classic.xz \
	$(Stable)/i586/base/srclist.classic.xz \
	$(Stable)/noarch/base/pkglist.classic.xz \
	$(Stable)/x86_64/base/pkglist.debuginfo.xz \
	$(Sisyphus)/x86_64-i586/base/pkglist.classic.xz

RPM_OPT_FLAGS ?= -O2 -g -Wall
# zstd -> BUILD/zstd-1.3.4
ZSTD_ADD = -Izstd/lib/dictBuilder -pthread

all: train blob2inc dumpinc rpmhdr.zstdict rpmhdrdict.h rpmhdrzdict.h
train: train.c zstd/lib/libzstd.a
	$(CC) $(RPM_OPT_FLAGS) $^ $(ZSTD_ADD) -o $@
zstd/lib/libzstd.a:
	sed -i- '/ kMaxK /s/ 2000 / 1000 /' zstd/lib/dictBuilder/cover.c
	make -C zstd/lib libzstd.a-mt
rpmhdr.zstdict: train
	xzcat -dc $(LISTS) |./train >rpmhdr.zstdict
blob2inc: blob2inc.c
	$(CC) $(RPM_OPT_FLAGS) $^ -llz4 -o $@
rpmhdrdict.h: blob2inc rpmhdr.zstdict
	./blob2inc rpmhdrdict <rpmhdr.zstdict >rpmhdrdict.h
rpmhdrzdict.h: blob2inc rpmhdr.zstdict
	./blob2inc -Z rpmhdrzdict <rpmhdr.zstdict >rpmhdrzdict.h
dumpinc: dumpinc.c rpmhdrdict.h rpmhdr.zstdict
	$(CC) $(RPM_OPT_FLAGS) -DINC=\"rpmhdrdict.h\" -DNAME=rpmhdrdict $< -o $@
	./dumpinc |cmp - rpmhdr.zstdict
