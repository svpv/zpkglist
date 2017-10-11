AM_CFLAGS = -Wall

lib_LTLIBRARIES = libzpkglist.la
libzpkglist_la_SOURCES = \
	reada.c lz4reader.c zstdreader.c xzreader.c \
	op-rpmheader.c op-zpkglist.c op-lz.c \
	zreader.c reader.c compress.c
reada.lo: CFLAGS += -fvisibility=hidden
lz4reader.lo: CFLAGS += -fvisibility=hidden @LZ4_CFLAGS@
zstdreader.lo: CFLAGS += -fvisibility=hidden @ZSTD_CFLAGS@
xzreader.lo: CFLAGS += -fvisibility=hidden @LZMA_CFLAGS@
libzpkglist_la_LIBADD = @LZ4_LIBS@ @ZSTD_LIBS@ @LZMA_LIBS@
libzpkglist_la_LDFLAGS = -no-undefined -Wl,--no-undefined

include_HEADERS = zpkglist.h

bin_PROGRAMS = zpkglist
zpkglist_SOURCES = main.c
main.lo: CFLAGS += @RPM_CFLAGS@
zpkglist_LDADD = libzpkglist.la @RPM_LIBS@
