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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <endian.h>
#include <lz4.h>
#include "zreader.h"
#include "error.h"
#include "reada.h"
#include "header.h"
#include "magic4.h"

struct zreader {
    struct fda *fda;
    bool eof;
    size_t contentSize;
    size_t buf1size;
    size_t jbufsize;
    char *buf1, *jbuf;
    // Keeps the last 8 bytes of the dictionary which are clobbered
    // to and fro with the first header's magic.
    char save[8];
    // The leading fields of a data frame to read.
    unsigned lead[3];
};

static int zreader_begin(struct zreader *z, const char *err[2])
{
    unsigned w[4];
    // Read <magic,size> + contentSize.
    ssize_t ret = reada(z->fda, w, sizeof w);
    if (ret < 0)
	return ERRNO("read"), -1;
    if (ret == 0)
	return 0;
    if (ret < sizeof w)
	return ERRSTR("unexpected EOF"), -1;
    if (w[0] != MAGIC4_W_ZPKGLIST)
	return ERRSTR("bad zpkglist magic"), -1;
    if (w[1] != htole32(16))
	return ERRSTR("bad zpkglist frame size"), -1;
    if (w[3])
	return ERRSTR("contentSize too big"), -1;
    z->contentSize = le32toh(w[2]);
    // Read <buf1size,jbufsize> + the dictionary frame header.
    ret = reada(z->fda, w, sizeof w);
    if (ret < 0)
	return ERRNO("read"), -1;
    // When contentSize=0, permit a truncated frame without <buf1size,jbufsize>.
    if (ret == 0 || ret == 8) {
	if (z->contentSize)
	    return ERRSTR("unexpected EOF"), -1;
	return 0;
    }
    if (ret != sizeof w)
	return ERRSTR("unexpected EOF"), -1;
    // Peek at the dictionary frame.
    if (w[2] != MAGIC4_W_ZPKGLIST_DICT)
	return ERRSTR("bad dictionary magic"), -1;
    size_t zsize = le32toh(w[3]);
    // LZ4 maxiumum compression ratio is 255, therfore assume that
    // the compressed size of a 64K dictionary cannot go below 257 bytes.
    if (zsize < 257 || zsize > LZ4_COMPRESSBOUND(64<<10))
	return ERRSTR("bad dictionary zsize"), -1;
    // Verify <buf1size,jbufsize>.
    z->buf1size = htole32(w[0]);
    // The buffer will be used to read the compressed dictionary.
    if (z->buf1size < zsize)
	return ERRSTR("bad buf1size"), -1;
    z->jbufsize = htole32(w[1]);
    // Jumbo frames cannot exceed header limits.
    if (z->jbufsize > headerMaxSize)
	return ERRSTR("bad jbufsize"), -1;
    // If there are jumbo frames, they cannot be too small either.
    if (z->jbufsize && z->jbufsize <= (128<<10))
	return ERRSTR("bad jbufsize"), -1;
    // According to the spec, buf1size cannot be too big.
    if (z->buf1size > zsize &&
	z->buf1size > (128<<10) + LZ4_COMPRESSBOUND(128<<10) &&
	z->buf1size > LZ4_COMPRESSBOUND(z->jbufsize))
	return ERRSTR("bad buf1size"), -1;

    // Allocate the buffer, first will be used to read in the dictionary,
    // then data frames.  The uncompressed dictionary will be placed at
    // the beginning, then goes the buf1size segment proper.  Need 12 extra
    // bytes to peek at the next frame's header.
    char *buf = malloc((64<<10) + z->buf1size + 12);
    if (!buf)
	return ERRNO("malloc"), -1;
    // Read the dictionary data + the first frame's header.
    ret = reada(z->fda, buf + (64 << 10), zsize + 12);
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
    if (z->lead[0] != MAGIC4_W_ZPKGLIST_FRAME)
	return free(buf), ERRSTR("bad data frame magic"), -1;
    // There is some data, it's time to check the contentSize.
    if (!z->contentSize || z->contentSize < z->jbufsize)
	return free(buf), ERRSTR("bad contentSize"), -1;
    // Decompress the dictionary.  The dictionary is placed right before
    // z->buf.  Compared to "external dictionary mode", this speeds up
    // subsequent decompression by a factor of 1.5.
    ret = LZ4_decompress_safe(buf + (64 << 10), buf, zsize, 64 << 10);
    if (ret != (64 << 10))
	return free(buf), ERROR("LZ4_decompress_safe", "cannot decompress dictionary"), -1;
    // Are we there yet? (c) Shrek
    z->buf1 = buf + (64 << 10);
    z->jbuf = NULL;
    memcpy(z->save, z->buf1 - 8, 8);
    return 1;
}

int zreader_open(struct zreader **zp, struct fda *fda, const char *err[2])
{
    struct zreader *z = malloc(sizeof *z);
    if (!z)
	return ERRNO("malloc"), -1;

    *z = (struct zreader) { fda };

    int rc = zreader_begin(z, err);
    if (rc <= 0)
	return free(z), rc;
    *zp = z;
    return rc;
}

ssize_t zreader_getFrame(struct zreader *z, void **bufp, off_t *posp, const char *err[2])
{
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
    off_t pos = tella(z->fda) - 12;
    int ret = reada(z->fda, z->zbuf, zsize + 12);
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
	if (z->lead[0] != MAGIC4_W_ZPKGLIST_FRAME)
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
    // Append the trailing magic (not on behalf of the frame size).
    // This may clobber z->zbuf, which should not be a problem by now.
    memcpy(z->buf + size, headerMagic, 8);
    *bufp = z->buf - 8;
    if (posp)
	*posp = pos;
    return size + 8;
}

void zreader_free(struct zreader *z)
{
    if (!z)
	return;
    if (z->buf1)
	free(z->buf1 - (64 << 10));
    if (z->jbuf)
	free(z->jbuf - 8);
    free(z);
}

unsigned zreader_contentSize(struct zreader *z)
{
    return z->contentSize;
}
