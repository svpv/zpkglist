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

    z->bulkBuf = NULL;
    z->hasLead = false;
    z->eof = false;
    z->hdrBuf = NULL;
    z->hdrBufSize = 0;

    *zp = z;
    return 1;
}

void zpkglistFree(struct zpkglistReader *z)
{
    if (!z)
	return;
    z->ops->opFree(z);
    free(z->bulkBuf);
    free(z->hdrBuf);
    free(z);
}

void zpkglistClose(struct zpkglistReader *z)
{
    if (!z)
	return;
    close(z->fda.fd);
    zpkglistFree(z);
}

static ssize_t zread1(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    while (1) {
	ssize_t n = z->ops->opRead(z, buf, size, err);
	if (n)
	    return n;

	const struct ops *ops;
	int rc = zpkglistBegin(&z->fda, &ops, err);
	if (rc <= 0)
	    return rc;
	if (ops == z->ops)
	    rc = z->ops->opReopen(z, err);
	else {
	    z->ops->opFree(z), z->opState = NULL;
	    z->ops = ops;
	    rc = z->ops->opOpen(z, err);
	}
	if (rc < 0)
	    return rc;
	assert(rc > 0);
    }
}

ssize_t zread(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    assert(size > 0);
    size_t total = 0;
    do {
	ssize_t n = zread1(z, buf, size, err);
	if (n < 0)
	    return -1;
	if (n == 0)
	    break;
	size -= n, buf = (char *) buf + n;
	total += n;
    } while (size);
    return total;
}

ssize_t generic_opBulk(struct zpkglistReader *z, void **bufp, const char *err[2])
{
    if (!z->bulkBuf) {
	z->bulkBuf = malloc(256 << 10);
	if (!z->bulkBuf)
	    return ERRNO("malloc"), -1;
    }
    ssize_t n = zread1(z, z->bulkBuf, 256 << 10, err);
    if (n <= 0)
	return n;
    *bufp = z->bulkBuf;
    return n;
}

ssize_t zpkglistBulk(struct zpkglistReader *z, void **bufp, const char *err[2])
{
    return z->ops->opBulk(z, bufp, err);
}

ssize_t zpkglistNextMalloc(struct zpkglistReader *z, struct HeaderBlob **blobp,
	int64_t *posp, const char *err[2])
{
    ssize_t ret = z->ops->opNextMalloc(z, posp, err);
    if (ret > 0)
	*blobp = z->hdrBuf, z->hdrBuf = NULL;
    return ret;
}

ssize_t zpkglistNextMallocP(struct zpkglistReader *z, struct HeaderBlob ***blobpp,
	int64_t *posp, const char *err[2])
{
    ssize_t ret = z->ops->opNextMalloc(z, posp, err);
    if (ret > 0)
	*blobpp = (void *) &z->hdrBuf;
    return ret;
}

ssize_t zpkglistNextView(struct zpkglistReader *z, struct HeaderBlob **blobp,
	int64_t *posp, const char *err[2])
{
    ssize_t ret = z->ops->opNextMalloc(z, posp, err);
    if (ret > 0)
	*blobp = z->hdrBuf;
    return ret;
}
