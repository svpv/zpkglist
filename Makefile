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
