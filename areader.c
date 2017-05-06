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
#include <unistd.h>
#include "reader.h"
#include "error.h"
#include "xread.h"
#include "header.h"

int aInit(struct aReader *a, int fd, char lead[16], const char *err[2])
{
    int dataSize = headerDataSize(lead);
    if (dataSize < 0)
	return -1;
    a->fd = fd;
    a->justOpened = true;
    memcpy(a->lead, lead, 16);
    a->dataSize = dataSize;
    return 1;
}

static bool aMoreBuf(struct aReader *a, size_t bufSize)
{
    size_t chunks = bufSize >> 16; // 64K chunks
    bufSize = (chunks + chunks / 2 + 1) << 16;
    free(a->buf);
    a->buf = malloc(bufSize);
    if (!a->buf)
	return false;
    a->bufSize = bufSize;
    return true;
}

static int aReadBuf(union u *u, void **bufp, const char *err[2])
{
    struct aReader *a = &u->a;
    if (a->eof)
	return 0;
    if (!a->bufSize && !aMoreBuf(a, 1))
	return ERRNO("malloc"), -1;
    *bufp = a->buf;
    int ret;
    if (!a->justOpened)
	ret = xread(a->fd, a->buf, 64 << 10);
    else {
	a->justOpened = false;
	memcpy(a->buf, a->lead, 16);
	ret = xread(a->fd, a->buf + 16, (64 << 10) - 16);
	if (ret >= 0)
	    return ret + 16;
    }
    if (ret < 0)
	ERRNO("read");
    return ret;
}

static void aClose(union u *u)
{
    struct aReader *a = &u->a;
    close(a->fd);
    free(a->buf);
}

struct ops aOps = {
    .opReadBuf = aReadBuf,
    .opClose = aClose,
};
