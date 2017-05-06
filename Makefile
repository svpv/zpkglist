RPM_OPT_FLAGS ?= -O2 -g -Wall
all: libzpkglist.a zpkglist
libzpkglist.a: compress.o reader.o areader.o zreader.o error.o xread.o header.o
	$(AR) r $@ $^
compress.o: compress.c zpkglist.h error.h xread.h header.h train/rpmhdrdict.h train/rpmhdrzdict.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
error.o: error.c error.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
reader.o: reader.c reader.h zpkglist.h error.h xread.h header.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
areader.o: areader.c reader.h error.h xread.h header.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
zreader.o: zreader.c reader.h error.h xread.h header.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
xread.o: xread.c xread.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
header.o: header.c header.h
	$(CC) $(RPM_OPT_FLAGS) -fpic -c $<
zpkglist: main.c zpkglist.h error.h xread.h libzpkglist.a
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
