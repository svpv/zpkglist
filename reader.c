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
    &ops_zstd,
    &ops_xz,
};

static int zpkglistBegin(struct fda *fda, const struct ops **opsp, const char *err[2])
{
    unsigned w;
    ssize_t ret = peeka(fda, &w, 4);
    if (ret < 0)
	return ERRNO("read"), -1;
    if (ret == 0)
	return 0;
    if (ret < 4)
	return ERRSTR("unexpected EOF"), -1;
    assert(ret == 4);

    enum magic4 m4 = magic4(w);
    if (m4 == MAGIC4_UNKNOWN)
	return ERRSTR("unknown magic"), -1;

    *opsp = allOps[m4];
    return 1;
}

int zpkglistFdopen(struct zpkglistReader **zp, int fd, const char *err[2])
{
    struct zpkglistReader *z = malloc(sizeof *z);
    if (!z)
	return ERRNO("malloc"), -1;

    z->fda = (struct fda) { fd, z->fdabuf };

    int rc = zpkglistBegin(&z->fda, &z->ops, err);
    if (rc <= 0)
	return free(z), rc;

    if (!z->ops->opOpen(z, err))
	return free(z), -1;

    z->hasLead = false;
    z->eof = false;
    z->buf = NULL;
    z->bufSize = 0;

    *zp = z;
    return 1;
}

void zpkglistFree(struct zpkglistReader *z)
{
    if (!z)
	return;
    z->ops->opFree(z);
    free(z->buf);
    free(z);
}

void zpkglistClose(struct zpkglistReader *z)
{
    if (!z)
	return;
    close(z->fda.fd);
    zpkglistFree(z);
}

static int zpkglistConcat(struct zpkglistReader *z, const char *err[2])
{
    z->ops->opFree(z), z->reader = NULL;

    int rc = zpkglistBegin(&z->fda, &z->ops, err);
    if (rc <= 0)
	return rc;
    return z->ops->opOpen(z, err);
}

#define ConcatRead(n, opReadCall)		\
    ssize_t n;					\
    do {					\
	n = z->ops->opReadCall;			\
	if (n > 0)				\
	    break;				\
	if (n < 0)				\
	    return -1;				\
	int rc = zpkglistConcat(z, err);	\
	if (rc <= 0)				\
	    return rc;				\
    } while (1)

ssize_t zpkglistRead(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    // Backends assert that size > 0.
    ConcatRead(n, opRead(z, buf, size, err));
    return n;
}

ssize_t zpkglistBulk(struct zpkglistReader *z, void **bufp, const char *err[2])
{
    ConcatRead(n, opBulk(z, bufp, err));
    return n;
}

ssize_t zpkglistNextMalloc(struct zpkglistReader *z, struct HeaderBlob **blobp,
	int64_t *posp, const char *err[2])
{
    ConcatRead(n, opNextMalloc(z, posp, err));
    *blobp = z->buf, z->buf = NULL;
    return n;
}

ssize_t zpkglistNextMallocP(struct zpkglistReader *z, struct HeaderBlob ***blobpp,
	int64_t *posp, const char *err[2])
{
    ConcatRead(n, opNextMalloc(z, posp, err));
    *blobpp = (void *) &z->buf;
    return n;
}

ssize_t zpkglistNextView(struct zpkglistReader *z, struct HeaderBlob **blobp,
	int64_t *posp, const char *err[2])
{
    ConcatRead(n, opNextView(z, (void **) blobp, posp, err));
    // TODO: reallocate on a 4-byte boundary.
    return n;
}

int64_t zpkglistContentSize(struct zpkglistReader *z)
{
    return z->ops->opContentSize(z);
}
