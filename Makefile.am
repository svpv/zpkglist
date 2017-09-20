AM_CFLAGS = -Wall @LZ4_CFLAGS@ @ZSTD_CFLAGS@ @LZMA_CFLAGS@

lib_LTLIBRARIES = libzpkglist.la
libzpkglist_la_SOURCES = \
	reada.c lz4reader.c zstdreader.c xzreader.c \
	op-rpmheader.c op-zpkglist.c op-lz.c \
	zreader.c reader.c compress.c
libzpkglist_la_LIBADD = @LZ4_LIBS@ @ZSTD_LIBS@ @LZMA_LIBS@
libzpkglist_la_LDFLAGS = -no-undefined -Wl,--no-undefined

include_HEADERS = zpkglist.h

bin_PROGRAMS = zpkglist
zpkglist_SOURCES = main.c
zpkglist_LDADD = libzpkglist.la
