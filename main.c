// Copyright (c) 2017, 2018 Alexey Tourbin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <rpm/rpmlib.h>
#include "zpkglist.h"
#include "error.h"
#include "xwrite.h"
#include "header.h"

#define PROG "zpkglist"
#define warn(fmt, args...) fprintf(stderr, "%s: " fmt "\n", PROG, ##args)
#define die(fmt, args...) warn(fmt, ##args), exit(128) // like git

enum {
    OPT_HELP = 256,
    OPT_QF,
    OPT_PRINTSIZE,
    OPT_MALLOC,
    OPT_VIEW,
};

static const struct option longopts[] = {
    { "qf", required_argument, NULL, OPT_QF },
    { "print-content-size", no_argument, NULL, OPT_PRINTSIZE },
    { "decompress", no_argument, NULL, 'd' },
    { "uncompress", no_argument, NULL, 'd' },
    { "malloc", no_argument, NULL, OPT_MALLOC },
    { "view", no_argument, NULL, OPT_VIEW },
    { "help", no_argument, NULL, OPT_HELP },
    { NULL },
};

int main(int argc, char **argv)
{
    int c;
    bool usage = false;
    bool decode = false;
    bool nextView = false, nextMalloc = false;
    bool printsize = false;
    const char *qf = NULL;
    while ((c = getopt_long(argc, argv, "d", longopts, NULL)) != -1) {
	switch (c) {
	case 0:
	    break;
	case 'd':
	    decode = true;
	    break;
	case OPT_MALLOC:
	    decode = true, nextMalloc = true;
	    break;
	case OPT_VIEW:
	    decode = true, nextView = true;
	    break;
	case OPT_QF:
	    qf = optarg;
	    break;
	case OPT_PRINTSIZE:
	    printsize = true;
	    break;
	default:
	    usage = 1;
	}
    }
    if (argc > optind && !usage) {
	warn("too many arguments");
	usage = 1;
    }
    if (isatty(0) && !usage) {
	warn("%s data cannot be read from a terminal",
	    decode || qf || printsize ? "binary" : "compressed");
	usage = 1;
    }
    if (usage) {
	fprintf(stderr, "Usage: " PROG "[-d] [--qf=FMT] <pkglist\n");
	return 2;
    }
    if (!qf && !printsize && isatty(1))
	die("%s data cannot be written to a terminal",
	    decode ? "binary" : "compressed");
    if (qf && printsize)
	die("--qf=FMT and --print-content-size are mutually exclusive");
    posix_fadvise(0, 0, 0, POSIX_FADV_SEQUENTIAL);
    const char *func;
    const char *err[2];
    ssize_t ret;
    if (!decode && !qf && !printsize) {
	func = "zpkglistCompress";
	ret = zpkglistCompress(0, 1, NULL, NULL, err);
	if (ret == 0)
	    warn("empty input (valid output still written)");
    }
    else {
	struct zpkglistReader *z;
	func = "zpkglistFdopen";
	ret = zpkglistFdopen(&z, 0, err);
	if (ret == 0 && printsize)
	    puts("0");
	if (ret > 0) {
	    if (qf) {
		struct HeaderBlob *blob;
		func = "zpkglistNextMalloc";
		while ((ret = zpkglistNextMalloc(z, &blob, NULL, err)) > 0) {
		    Header h = headerImport(blob, ret, 0);
		    if (h == NULL) {
			func = err[0] = "headerImport",
			err[1] = "headerImport failed";
			ret = -1;
			break;
		    }
		    char *s = headerFormat(h, qf, &err[1]);
		    if (!s) {
			func = err[0] = "headerFormat";
			ret = -1;
			break;
		    }
		    fputs(s, stdout);
		    free(s);
		    headerFree(h);
		}
	    }
	    else if (printsize) {
		int64_t contentSize = zpkglistContentSize(z);
		if (contentSize < 0)
		    return 1; // unknown
		printf("%" PRId64 "\n", contentSize);
	    }
	    else if (nextMalloc && nextView) {
		struct HeaderBlob *blob;
		func = "zpkglistNextMalloc";
		for (unsigned i = 0; ; i++) {
		    if (i % 2)
			func = "zpkglistNextMalloc",
			ret  =  zpkglistNextMalloc(z, &blob, NULL, err);
		    else
			func = "zpkglistNextView",
			ret  =  zpkglistNextView(z, &blob, NULL, err);
		    if (ret <= 0)
			break;
		    bool ok = xwrite(1, headerMagic, 8) && xwrite(1, blob, ret);
		    if (i % 2)
			free(blob);
		    if (!ok) {
			func = "main";
			ERRNO("write");
			ret = -1;
			break;
		    }
		}
	    }
	    else if (nextMalloc) {
		struct HeaderBlob *blob;
		func = "zpkglistNextMalloc";
		while ((ret = zpkglistNextMalloc(z, &blob, NULL, err)) > 0) {
		    bool ok = xwrite(1, headerMagic, 8) && xwrite(1, blob, ret);
		    free(blob);
		    if (!ok) {
			func = "main";
			ERRNO("write");
			ret = -1;
			break;
		    }
		}
	    }
	    else if (nextView) {
		struct HeaderBlob *blob;
		func = "zpkglistNextView";
		while ((ret = zpkglistNextView(z, &blob, NULL, err)) > 0)
		    if (!(xwrite(1, headerMagic, 8) && xwrite(1, blob, ret))) {
			func = "main";
			ERRNO("write");
			ret = -1;
			break;
		    }
	    }
	    else {
		void *buf;
		func = "zpkglistBulk";
		while ((ret = zpkglistBulk(z, &buf, err)) > 0)
		    if (!xwrite(1, buf, ret)) {
			func = "main";
			ERRNO("write");
			ret = -1;
			break;
		    }
	    }
	    zpkglistClose(z);
	}
    }
    if (ret < 0) {
	if (strcmp(func, err[0]) == 0)
	    die("%s: %s", err[0], err[1]);
	else
	    die("%s: %s: %s", func, err[0], err[1]);
    }
    return 0;
}
