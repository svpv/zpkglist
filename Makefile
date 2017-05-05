RPM_OPT_FLAGS ?= -O2 -g -Wall
all: libzpkglist.a zpkglist
libzpkglist.a: compress.o xread.o header.o
	$(AR) r $@ $^
compress.o: compress.c zpkglist.h xread.h header.h train/rpmhdrdict.h train/rpmhdrzdict.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
xread.o: xread.c xread.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
header.o: header.c header.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
zpkglist: main.c zpkglist.h libzpkglist.a
	$(CC) $(RPM_OPT_FLAGS) $< -o $@ libzpkglist.a -llz4
DESTDIR =
PREFIX = /usr
BINDIR = $(PREFIX)/bin
INCLUDEDIR = $(PREFIX)/include
LIBDIR = `getconf LIBDIR`
install: all zpkglist.h
	install -pD -m755 {,$(DESTDIR)$(BINDIR)/}zpkglist
	install -pD -m644 {,$(DESTDIR)$(INCLUDEDIR)/}zpkglist.h
	install -pD -m644 {,$(DESTDIR)$(LIBDIR)/}libzpkglist.a
