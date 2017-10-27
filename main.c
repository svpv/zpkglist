// Copyright (c) 2017 Alexey Tourbin
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
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <rpm/rpmlib.h>
#include "zpkglist.h"
#include "error.h"
#include "xwrite.h"

#define PROG "zpkglist"

enum {
    OPT_QF = 256,
};

static const struct option longopts[] = {
    { "qf", required_argument, NULL, OPT_QF },
    { "help", no_argument, NULL, 'h' },
    { NULL },
};

int main(int argc, char **argv)
{
    int c;
    bool usage = false;
    bool decode = false;
    const char *qf = NULL;
    while ((c = getopt_long(argc, argv, "dh", longopts, NULL)) != -1) {
	switch (c) {
	case 0:
	    break;
	case 'd':
	    decode = true;
	    break;
	case OPT_QF:
	    qf = optarg;
	    break;
	default:
	    usage = 1;
	}
    }
    if (argc > optind && !usage) {
	fprintf(stderr, PROG ": too many arguments\n");
	usage = 1;
    }
    if (usage) {
	fprintf(stderr, "Usage: " PROG "[-d] [--qf=FMT] <pkglist\n");
	return 1;
    }
    assert(!isatty(0));
    posix_fadvise(0, 0, 0, POSIX_FADV_SEQUENTIAL);
    if (!qf)
	assert(!isatty(1));
    const char *func;
    const char *err[2];
    int ret;
    if (!decode && !qf) {
	func = "zpkglistCompress";
	ret = zpkglistCompress(0, 1, err, NULL, NULL);
	if (ret == 0)
	    fprintf(stderr, PROG ": warning: empty input (valid output still written)\n");
    }
    else {
	struct zpkglistReader *z;
	func = "zpkglistFdopen";
	ret = zpkglistFdopen(&z, 0, err);
	if (ret > 0) {
	    if (qf) {
		void *blob;
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
	    fprintf(stderr, "%s: %s\n", err[0], err[1]);
	else
	    fprintf(stderr, "%s: %s: %s\n", func, err[0], err[1]);
	return 1;
    }
    return 0;
}
