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
#include <unistd.h> // close
#include <endian.h>
#include "zpkglist.h"
#include "reader.h"
#include "reada.h"
#include "error.h"
#include "magic4.h"

static const struct ops *allOps[] = {
    /* The same order as magic4. */
    &ops_rpmheader,
    &ops_zpkglist,
    &ops_lz4,
    &ops_zstd,
    &ops_xz,
};

int zpkglistFdopen(int fd, struct zpkglistReader **zp, const char *err[2])
{
    struct zpkglistReader *z = malloc(sizeof *z);
    if (!z)
	return ERRNO("malloc"), -1;

    z->fda = (struct fda) { fd, z->fdabuf };

    unsigned w;
    int ret = peeka(&z->fda, &w, 4);
    if (ret < 0)
	return free(z), ERRNO("read"), -1;
    if (ret == 0)
	return free(z), 0;
    if (ret < 4)
	return free(z), ERRSTR("unexpected EOF"), -1;
    assert(ret == 4);

    enum magic4 m4 = magic4(w);
    if (m4 == MAGIC4_UNKNOWN)
	return free(z), ERRSTR("unknown magic"), -1;

    z->ops = allOps[m4];

    if (!z->ops->opOpen(z, err))
	return free(z), -1;

    return 1;
}

void zpkglistClose(struct zpkglistReader *z)
{
    assert(z);
    z->ops->opFree(z);
    close(z->fda.fd);
    free(z);
}

ssize_t zpkglistRead(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    assert(size > 0);
    return z->ops->opRead(z, buf, size, err);
}

ssize_t zpkglistBulk(struct zpkglistReader *z, void **bufp, const char *err[2])
{
    return z->ops->opBulk(z, bufp, err);
}
