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
    uint64_t contentSize;
    size_t buf1size, jbufsize;
    char *buf1, *jbuf;
    // The sum of (8 + uncompressed size) of the processed frames,
    // should sum up to contentSize if the reads are sequential.
    uint64_t contentSizeSoFar;
    bool sequential;
    // Stream status.
    bool eof, err, pad;
    // Keeps the last 8 bytes of the dictionary which are clobbered
    // to and fro with the first header's magic.
    char save[8];
    // The leading fields of a data frame to read.
    unsigned lead[3];
};

static int zreader_begin(struct zreader *z, const char *err[2])
{
    // Read the leading frame.
    struct {
	unsigned magic;
	unsigned size16;
	uint64_t total;
	unsigned buf1size;
	unsigned jbufsize;
    } frame0;
    ssize_t ret = reada(z->fda, &frame0, sizeof frame0);
    if (ret < 0)
	return ERRNO("read"), -1;
    if (ret == 0)
	return 0;
    if (ret != sizeof frame0)
	return ERRSTR("unexpected EOF"), -1;
    if (frame0.magic != MAGIC4_W_ZPKGLIST)
	return ERRSTR("bad zpkglist magic"), -1;
    if (frame0.size16 != htole32(16))
	return ERRSTR("bad zpkglist frame size"), -1;
    z->buf1size = le32toh(frame0.buf1size);
    z->jbufsize = le32toh(frame0.jbufsize);
    z->contentSize = le64toh(frame0.total);

    // Validate the sizes:
    // contentSize and buf1size must be either both zero or both non-zero.
    if (!z->buf1size ^ !z->contentSize)
	return ERRSTR("bad buf1size"), -1;
    // If there are jumbo frames:
    if (z->jbufsize) {
	// buf1size must also be non-zero.
	if (!z->buf1size)
	    return ERRSTR("bad buf1size"), -1;
	// Jumbo frames cannot exceed header limits.
	// Or contentSize limits, for that matter.
	if (z->jbufsize > headerMaxSize ||
	    z->jbufsize > z->contentSize)
	    return ERRSTR("bad jbufsize"), -1;
	// They cannot be too small either.
	if (z->jbufsize <= (128<<10))
	    return ERRSTR("bad jbufsize"), -1;
    }
    // buf1size cannot be too big.
    if (z->buf1size > (128<<10) + LZ4_COMPRESSBOUND(128<<10) &&
	z->buf1size > LZ4_COMPRESSBOUND(z->jbufsize))
	return ERRSTR("bad buf1size"), -1;

    // Peek at the dictionary frame.
    unsigned w[2];
    ret = peeka(z->fda, w, sizeof w);
    if (ret < 0)
	return ERRNO("read"), -1;
    // Do we have a dictionary magic?
    if (ret < 4 || w[0] != MAGIC4_W_ZPKGLIST_DICT) {
	// No dictionary magic, no content?
	// Cannot just return 0, which would indicate physical EOF.
	// Since there is a valid frame, return an EOF object.
	if (z->contentSize == 0)
	    return z->eof = true, 1;
	if (ret < 4)
	    return ERRSTR("unexpected EOF"), -1;
	return ERRSTR("bad dictionary magic"), -1;
    }
    // There is a dictionary magic.  If the contentSize is 0, this means that
    // the file wasn't written properly (the leading frame wasn't overwritten).
    if (z->contentSize == 0)
	return ERRSTR("unexpected dictionary after blank frame"), -1;
    // Partial dictionary frame header?  No pasaran.
    if (ret != sizeof w)
	return ERRSTR("unexpected EOF"), -1;
    // Taking it, as if with reada().
    z->fda->cur += sizeof w;

    // Got the compressed dictionary size.
    size_t zsize = le32toh(w[1]);
    // LZ4 maxiumum compression ratio is 255, therfore assume that
    // the compressed size of a 64K dictionary cannot go below 257 bytes.
    if (zsize < 257 || zsize > LZ4_COMPRESSBOUND(64<<10))
	return ERRSTR("bad dictionary zsize"), -1;
    // The buffer will be used to read the compressed dictionary.
    if (z->buf1size < zsize)
	return ERRSTR("bad buf1size"), -1;

    // Allocate the buffer, first will be used to read in the dictionary,
    // then data frames.  The uncompressed dictionary will be placed at
    // the beginning, then goes the buf1size segment proper.
    char *buf = malloc((64<<10) + z->buf1size);
    if (!buf)
	return ERRNO("malloc"), -1;
    // Read the compressed dictionary.
    ret = reada(z->fda, buf + (64 << 10), zsize);
    if (ret < 0)
	return free(buf), ERRNO("read"), -1;
    if (ret != zsize)
	return free(buf), ERRSTR("unexpected EOF"), -1;

    // The contentSize is non-zero, so we expect at least one data frame.
    ret = reada(z->fda, z->lead, 12);
    if (ret < 0)
	return free(buf), ERRNO("read"), -1;
    if (ret != 12)
	return free(buf), ERRSTR("unexpected EOF"), -1;
    // Verify the first data frame's magic.  Unless the magic is valid,
    // we shouldn't even try to uncompress the dictionary - who knows
    // what we've read?  Pushkin knows?
    if (z->lead[0] != MAGIC4_W_ZPKGLIST_DATA)
	return free(buf), ERRSTR("bad data frame magic"), -1;

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
    z->sequential = true;
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

ssize_t zreader_getFrame(struct zreader *z, void **bufp, off_t *posp,
			 bool mallocJumbo, const char *err[2])
{
    if (z->err)
	return ERRSTR("pending error"), -1;
    if (z->eof)
	return 0;

    // The frame has already been peeked upon, decode the sizes.
    size_t zsize = le32toh(z->lead[1]) - 4;
    size_t size = le32toh(z->lead[2]);
    void *zbuf;
    // Validate the size, and check that zsize fits into the buffer.
    if (size > (128<<10)) {
	if (size > z->jbufsize)
	    return ERRSTR("bad data size"), -(z->err = true);
	if (zsize > z->buf1size)
	    return ERRSTR("bad data zsize"), -(z->err = true);
	zbuf = z->buf1;
    }
    else {
	if (size < 8) // at least (il,dl)
	    return ERRSTR("bad data size"), -(z->err = true);
	if (size + zsize > z->buf1size)
	    return ERRSTR("bad data size+zsize"), -(z->err = true);
	zbuf = z->buf1 + size;
    }
    // Further check that zsize is consistent with the size.
    if (!zsize || zsize > LZ4_COMPRESSBOUND(size))
	return ERRSTR("bad data zsize"), -(z->err = true);

    // Check the size against contentSize.
    z->contentSizeSoFar += 8 + size;
    if (z->contentSizeSoFar > z->contentSize)
	return ERRSTR("bad data size"), -(z->err = true);

    // About to read, remember the position.
    off_t pos = tella(z->fda) - 12;

    // Read the frame's compressed data.
    ssize_t ret = reada(z->fda, zbuf, zsize);
    if (ret < 0)
	return ERRNO("read"), -(z->err = true);
    if (ret != zsize)
	return ERRSTR("unexpected EOF"), -(z->err = true);

    // Peek at the next frame.
    ret = peeka(z->fda, z->lead, 12);
    if (ret < 0)
	return ERRNO("read"), -(z->err = true);
    // Do we have the magic?
    if (ret < 4 || z->lead[0] != MAGIC4_W_ZPKGLIST_DATA) {
	// No magic, possibly EOF.  If the reads have been sequential,
	// we have a reliable check for EOF based on contentSize.
	if (z->sequential && z->contentSizeSoFar != z->contentSize)
	    return ERRSTR("bad contentSize"), -(z->err = true);
	// Assume it's EOF.
	z->eof = true;
    }
    else {
	// Partial frame header?  No pasaran.
	if (ret != 12)
	    return ERRSTR("unexpected EOF"), -(z->err = true);
	// Okay, taking it, as if with reada().
	z->fda->cur += 12;
    }

    // Jumbo frame?
    if (size > (128<<10)) {
	void *buf;
	// Malloc requested?
	if (mallocJumbo)
	    buf = malloc(size);
	else {
	    // Will uncompress into z->jbuf.
	    if (!z->jbuf) {
		z->jbuf = malloc(8 + z->jbufsize);
		if (z->jbuf) {
		    // Implicit magic bytes.
		    memcpy(z->jbuf, headerMagic, 8);
		    z->jbuf += 8;
		}
	    }
	    buf = z->jbuf;
	}
	if (!buf)
	    return ERRNO("malloc"), -(z->err = true);
	// Uncompress without dictionary.
	int zret = LZ4_decompress_safe(zbuf, buf, zsize, size);
	if (zret != size) {
	    if (buf != z->jbuf)
		free(buf);
	    return ERROR("LZ4_decompress_safe", "decompression failed"),
		   -(z->err = true);
	}
	*bufp = buf;
	// Malloc'd jumbo frame signaled with big negative return.
	return buf == z->jbuf ? size : -size;
    }

    // Restore the last bytes of the dictionary.
    memcpy(z->buf1 - 8, z->save, 8);
    // Uncompress with dictionary.
    int zret = LZ4_decompress_safe_usingDict(zbuf, z->buf1, zsize, size,
					     z->buf1 - (64 << 10), 64 << 10);
    if (zret != size)
	return ERROR("LZ4_decompress_safe_usingDict", "decompression failed"),
	       -(z->err = true);
    // Prepend the magic, clobbers the last bytes of the dictionary.
    memcpy(z->buf1 - 8, headerMagic, 8);
    *bufp = z->buf1;
    if (posp)
	*posp = pos;
    return size;
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
