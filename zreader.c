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
#include <unistd.h>
#include <endian.h>
#include <lz4.h>
#include "reader.h"
#include "error.h"
#include "xread.h"
#include "header.h"

int zInit(struct zReader *z, int fd, char lead[16], const char *err[2])
{
    unsigned w[4];
    memcpy(w, lead, 16);
    if (w[3])
	return ERRSTR("input too big"), -1;
    z->fd = fd;
    z->contentSize = le32toh(w[2]);
    // Read the rest of the leading frame + the dictionary frame header.
    int ret = xread(fd, w, 16);
    if (ret < 0)
	return ERRNO("read"), -1;
    // Permit a truncated frame with contentSize but without (maxSize,zmaxSize).
    if (ret == 0 || ret == 8) {
	if (z->contentSize)
	    return ERRSTR("unexpected EOF"), -1;
	return 0;
    }
    if (ret != 16)
	return ERRSTR("unexpected EOF"), -1;
    // Peek at the dictionary frame.
    if (le32toh(w[2]) != 0x184D2A56)
	return ERRSTR("bad dictionary magic"), -1;
    size_t zsize = le32toh(w[3]) - 4;
    if (!zsize || zsize >= (64 << 10))
	return ERRSTR("bad dictionary zsize"), -1;
    // The input is not empty, there's a dictionary.  Permit the situation
    // in which there's nothing after the dictionary.  But to get there, the
    // dictionary needs to be loaded, which in turn requires a buffer to be
    // allocated.  In other words, only check that the sizes are not too big.
    z->maxSize = le32toh(w[0]);
    z->zmaxSize = le32toh(w[1]);
    if (z->maxSize > headerMaxSize || z->maxSize > z->contentSize)
	return ERRSTR("bad maxSize"), -1;
    if (z->zmaxSize > LZ4_COMPRESSBOUND(z->maxSize))
	return ERRSTR("bad zmaxSize"), -1;
    // Allocate the buffer, first used for dictionary, then for data frames.
    size_t alloc = z->maxSize + z->zmaxSize + 12;
    if (alloc < zsize + 12)
	alloc = zsize + 12;
    char *buf = malloc((64 << 10) + alloc);
    if (!buf)
	return ERRNO("malloc"), -1;
    // Read the dictionary data + the first frame's header.
    ret = xread(fd, buf + (64 << 10), zsize + 12);
    if (ret < 0)
	return free(buf), ERRNO("read"), -1;
    // Nothing after the dictionary?
    if (ret == zsize) {
	if (z->contentSize)
	    return free(buf), ERRSTR("unexpected EOF"), -1;
	return free(buf), 0;
    }
    if (ret != zsize + 12)
	return free(buf), ERRSTR("unexpected EOF"), -1;
    // Peek at the first frame's header.
    memcpy(z->lead, buf + (64 << 10) + zsize, 12);
    if (le32toh(z->lead[0]) != 0x184D2A57)
	return free(buf), ERRSTR("bad data frame magic"), -1;
    // There is some data, it's time to check the sizes.  E.g. if maxSize is 0,
    // the allocated buffer is too small, it's not gonna work.
    if (!z->contentSize)
	return free(buf), ERRSTR("bad contentSize"), -1;
    if (!z->maxSize)
	return free(buf), ERRSTR("bad maxSize"), -1;
    if (!z->zmaxSize)
	return free(buf), ERRSTR("bad zmaxSize"), -1;
    // Decompress the dictionary.  The dictionary is placed right before
    // z->buf.  Compared to "external dictionary mode", this speeds up
    // subsequent decompression by a factor of 1.5.
    ret = LZ4_decompress_safe(buf + (64 << 10), buf, zsize, 64 << 10);
    if (ret != (64 << 10))
	return free(buf), ERROR("LZ4_decompress_safe", "cannot decompress dictionary"), -1;
    // Are we there yet? (c) Shrek
    z->buf = buf + (64 << 10);
    z->zbuf = z->buf + z->maxSize;
    memcpy(z->save, z->buf - 8, 8);
    return 1;
}

static int zReadBuf(union u *u, void **bufp, const char *err[2])
{
    struct zReader *z = &u->z;
    if (z->eof)
	return 0;
    // The frame has already been peeked upon, validate the sizes.
    size_t zsize = le32toh(z->lead[1]) - 4;
    size_t size = le32toh(z->lead[2]);
    if (!size || size > z->maxSize)
	return ERRSTR("bad data size"), -1;
    if (!zsize || zsize > z->zmaxSize || zsize > LZ4_COMPRESSBOUND(size))
	return ERRSTR("bad data zsize"), -1;
    // Read this frame's data + the next frame's header.
    int ret = xread(z->fd, z->zbuf, zsize + 12);
    if (ret < 0)
	return ERRNO("read"), -1;
    if (ret == zsize)
	z->eof = true;
    else if (ret != zsize + 12)
	return ERRSTR("unexpected EOF"), -1;
    else {
	// Save the next frame's header for the next call.
	memcpy(z->lead, z->zbuf + zsize, 12);
	// Peek at the next frame's header.
	if (le32toh(z->lead[0]) != 0x184D2A57)
	    return ERRSTR("bad data frame magic"), -1;
    }
    // Restore the last bytes of the dictionary.
    memcpy(z->buf - 8, z->save, 8);
    // Decompress the frame.  This "fast" function is somewhat unsafe: although
    // it does not write past z->buf + size, it can read past z->zbuf + zsize.
    // I opt for speed nonetheless.  This file format has not been designed for
    // long-term storage, but rather for use with APT.  In this usage scenario,
    // compressed package lists typically get updated with every "apt-get update"
    // command, hence there are no checksums, etc.  Note that the overall file
    // structure is checked rather meticulously (e.g. decompression doesn't even
    // start before the next frame's magic is verified).  This should be enough
    // to protect against unintended data corruption.
    ret = LZ4_decompress_fast_usingDict(z->zbuf, z->buf, size, z->buf - (64 << 10), 64 << 10);
    if (ret != zsize)
	return ERROR("LZ4_decompress_fast_usingDict", "decompression failed"), -1;
    // Prepend the missing magic.
    memcpy(z->buf - 8, headerMagic, 8);
    *bufp = z->buf - 8;
    return size + 8;
}

static void zClose(union u *u)
{
    struct zReader *z = &u->z;
    close(z->fd);
    if (z->buf) {
	free(z->buf - (64 << 10));
	z->buf = NULL;
    }
}

struct ops zOps = {
    .opReadBuf = zReadBuf,
    .opClose = zClose,
};