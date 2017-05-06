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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <endian.h>
#include "zpkglist.h"
#include "reader.h"
#include "xread.h"
#include "error.h"
#include "header.h"

struct zpkglistReader {
    struct ops ops;
    union u u;
};

int zpkglistFdopen(int fd, struct zpkglistReader **zp, const char *err[2])
{
    char lead[16];
    int ret = xread(fd, lead, 16);
    if (ret < 0)
	return ERRNO("read"), -1;
    if (ret == 0)
	return 0;
    if (ret != 16)
	return ERRSTR("unexpected EOF"), -1;
    struct zpkglistReader *z = calloc(1, sizeof *z);
    if (!z)
	return ERRNO("malloc"), -1;
    // Dispatch according to the leading magic.
    unsigned frameHeader[] = { htole32(0x184D2A55), htole32(16) };
    if (memcmp(lead, frameHeader, 8) == 0) {
	z->ops = zOps;
	ret = zInit(&z->u.z, fd, lead, err);
    }
    else if (memcmp(lead, headerMagic, 8) == 0) {
	z->ops = aOps;
	ret = aInit(&z->u.a, fd, lead, err);
    }
    else {
	ERRSTR("unknown file format");
	ret = -1;
    }
    if (ret > 0)
	*zp = z;
    else
	free(z);
    return ret;
}

void zpkglistClose(struct zpkglistReader *z)
{
    if (!z)
	return;
    z->ops.opClose(&z->u);
    free(z);
}

int zpkglistReadBuf(struct zpkglistReader *z, void **bufp, const char *err[2])
{
    return z->ops.opReadBuf(&z->u, bufp, err);
}
