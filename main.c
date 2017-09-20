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
#include "zpkglist.h"
#include "error.h"
#include "xread.h"

#define PROG "zpkglist"

int main(int argc, char **argv)
{
    posix_fadvise(0, 0, 0, POSIX_FADV_SEQUENTIAL);
    assert(!isatty(0));
    assert(!isatty(1));
    argc--, argv++;
    bool decode = false;
    if (argc > 0 && strcmp(argv[0], "-d") == 0) {
	argc--, argv++;
	decode = true;
    }
    assert(argc == 0);
    const char *func;
    const char *err[2];
    int ret;
    if (!decode) {
	func = "zpkglistCompress";
	ret = zpkglistCompress(0, 1, err, NULL, NULL);
	if (ret == 0)
	    fprintf(stderr, PROG ": warning: empty input (valid output still written)\n");
    }
    else {
	struct zpkglistReader *z;
	func = "zpkglistFdopen";
	ret = zpkglistFdopen(0, &z, err);
	if (ret > 0) {
	    void *buf;
	    func = "zpkglistBulk";
	    while ((ret = zpkglistBulk(z, &buf, err)) > 0)
		if (!xwrite(1, buf, ret)) {
		    func = "main";
		    ERRNO("write");
		    ret = -1;
		    break;
		}
	    zpkglistClose(z);
	}
    }
    if (ret < 0) {
	if (strcmp(func, err[0]) == 0)
	    fprintf(stderr, "%s: %s\n", err[0], err[1]);
	else
	    fprintf(stderr, "%s: %s: %s\n", func, err[0], err[1]);
	if (decode)
	    fprintf(stderr, PROG ": decompression failed\n");
	else
	    fprintf(stderr, PROG ": compression failed\n");
	return 1;
    }
    return 0;
}
