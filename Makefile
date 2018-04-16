NAME = zpkglist
SONAME = lib$(NAME).so.0

all: lib$(NAME).so $(NAME)
lib$(NAME).so: $(SONAME)
	ln -sf $< $@
clean:
	rm -f lib$(NAME).so $(SONAME) $(NAME)

SRC = reader.c zreader.c xzreader.c zstdreader.c reada.c \
      compress.c op-rpmheader.c op-zpkglist.c op-lz.c
HDR = reader.h zreader.h xzreader.h zstdreader.h reada.h \
      zpkglist.h error.h header.h magic4.h xwrite.h \
      train/rpmhdrzdict.h op-lz-template.C

RPM_OPT_FLAGS ?= -O2 -g -Wall
WEXTRA = -Wextra -Wno-{sign-compare,missing-field-initializers,unused-parameter}
STD = -std=gnu11 -D_GNU_SOURCE
LFS = $(shell getconf LFS_CFLAGS)
LTO = -flto
COMPILE = $(CC) $(RPM_OPT_FLAGS) $(WEXTRA) $(STD) $(LFS) $(LTO)

SHARED = -fpic -shared -Wl,-soname=$(SONAME) -Wl,--no-undefined
LIBS = -llz4 -llzma -lzstd

$(SONAME): $(SRC) $(HDR)
	$(COMPILE) -o $@ $(SRC) $(SHARED) $(LIBS)

RPATH = -Wl,-rpath,$$PWD

$(NAME): main.c lib$(NAME).so
	$(COMPILE) -o $@ $^ -lrpm $(RPATH)
