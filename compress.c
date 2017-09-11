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
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <endian.h>
#include <lz4.h>
#include "zpkglist.h"
#include "error.h"
#include "xread.h"
#include "header.h"

struct Z {
    // The clean LZ4 state is initialized only once.  (This is a relatively
    // expensive step, because the dictionary has to be scanned and hashed.)
    // When compressing each frame, the clean state is quickly cloned.
    // This technique requires access to private parts of LZ4 API.
    LZ4_stream_t stream0, stream;
    // The input buffer (which contains a few rpm header blobs) and the output
    // buffer (with compressed LZ4 data).
    char *buf, *zbuf;
    size_t bufSize, zbufSize;
};

#include "train/rpmhdrdict.h"
#include "train/rpmhdrzdict.h"

// The size of struct Z is 32K+, had better be malloc'd.
static struct Z *zNew(const char *err[2])
{
    struct Z *z = malloc(sizeof *z);
    if (!z)
	return ERRNO("malloc"), NULL;
    // Initialize the clean state.
    memset(&z->stream0, 0, sizeof z->stream0);
    // Preload the dictionary.
    int zret = LZ4_loadDict(&z->stream0, rpmhdrdict, 64 << 10);
    assert(zret == sizeof rpmhdrdict);
    // Check our assumptions about where the dictionary is.
    // When z->buf is allocated, this pointer will be changed.
    assert(z->stream0.internal_donotuse.dictionary == (void *) rpmhdrdict);
    // Nullify the buffers.
    z->bufSize = z->zbufSize = 0;
    z->buf = z->zbuf = NULL;
    return z;
}

// Called when the buffer is not large enough, before filling it, including
// the first allocation.  Explores the technique of placing the dictionary
// right before the data to be compressed, which speeds up compression with
// the dictionary by a factor of 1.4.
static bool zMoreBuf(struct Z *z, size_t size, const char *err[2])
{
    // z->buf and z->zbuf are allocated in a single chunk.
    // The dictionary is placed right before z->buf.
    // z->buf acutally points to malloc() + dictSize.
    if (z->buf) {
	free(z->buf - (64 << 10));
	z->buf = NULL;
    }
    // It's gonna be an mmap, so the size basically doesn't matter.
    size_t mb = size >> 20;
    z->bufSize = (mb + mb / 2 + 1) << 20;
    // Compressed frames start with a frame header.
    z->zbufSize = 12 + LZ4_COMPRESSBOUND(z->bufSize);
    char *buf = malloc((64 << 10) + z->bufSize + z->zbufSize);
    if (!buf)
	return ERRNO("malloc"), false;
    z->buf = buf + (64 << 10);
    z->zbuf = z->buf + z->bufSize;
    // Copy the dictionary.
    memcpy(buf, rpmhdrdict, 64 << 10);
    // Update the link to the dictionary in the LZ4 stream.
    z->stream0.internal_donotuse.dictionary = (void *) buf;
    return true;
}

// Called to start compression, after the buffer is filled.
static void zBegin(struct Z *z)
{
    // The buffer had better be allocated.
    assert(z->buf);
    // Copy the clean state (struct assignment).
    z->stream = z->stream0;
}

static void zFree(struct Z *z)
{
    if (z->buf)
	free(z->buf - (64 << 10));
    free(z);
}

// Statistics for the leading frame.
struct stats {
    size_t total;
    unsigned maxSize;
    unsigned zmaxSize;
};

static bool zLoop(struct Z *z, struct stats *stats, int in, int out, const char *err[2],
		  void (*hash)(void *buf, unsigned size, void *arg), void *arg)
{
    // Load the leading bytes of the first header.
    char lead[16];
    int ret = xread(in, lead, 16);
    // If it's EOF or an error, do nothing.
    if (ret < 0)
	return ERRNO("read"), false;
    if (ret == 0)
	return true;
    if (ret < 16)
	return ERRSTR("unexpected EOF"), false;
    if (!headerCheckMagic(lead))
	return ERRSTR("bad header magic"), false;

    // The size of the header's data after (il,dl).
    int dataSize = headerDataSize(lead);
    if (dataSize < 0)
	return ERRSTR("bad header size"), false;

    // Write the dictionary.
    if (!xwrite(out, rpmhdrzdict, sizeof rpmhdrzdict))
	return ERRNO("write"), false;

    while (1) {
	// Minimum buffer size is 1M, no need to reallocate in the inner loop.
	size_t needBufSize = 8 + dataSize + 16;
	if (needBufSize > z->bufSize && !zMoreBuf(z, needBufSize, err))
	    return false;

	char *cur = z->buf;
	bool eof = false;

	// Trying to fit four headers into 256K.
	for (int i = 0; i < 4; i++) {
	    // Put this header's leading bytes.
	    // The very first magic won't be written.
	    if (i == 0) {
		memcpy(cur, lead + 8, 8);
		cur += 8;
	    }
	    else {
		memcpy(cur, lead, 16);
		cur += 16;
	    }
	    // Read this header's data + the next header's leading bytes.
	    ret = xread(in, cur, dataSize + 16);
	    if (ret < 0)
		return ERRNO("read"), false;
	    cur += dataSize;
	    if (ret == dataSize) {
		eof = true;
		break;
	    }
	    if (ret != dataSize + 16)
		return ERRSTR("unexpected EOF"), false;
	    // Save the next header's leading bytes for the next iteration.
	    memcpy(lead, cur, 16);
	    // Verify the next header's magic - otherwise, we aren't even sure
	    // we got the right size.
	    if (!headerCheckMagic(lead))
		return ERRSTR("bad header magic"), false;
	    // The next header is for the next iteration.
	    dataSize = headerDataSize(lead);
	    if (dataSize < 0)
		return ERRSTR("bad header size"), false;
	    // Does the next header fit in?
	    if (cur - z->buf + dataSize > (256 << 10))
		break;
	}

	// Hash the data.  The leading magic is restored by clobbering
	// to and fro the last eight bytes of the dictionary.
	size_t fill = cur - z->buf;
	if (hash) {
	    char save[8];
	    char *pre = z->buf - 8;
	    memcpy(save, pre, 8);
	    memcpy(pre, headerMagic, 8);
	    hash(pre, fill + 8, arg);
	    memcpy(pre, save, 8);
	}

	// Compress the frame.
	zBegin(z);
	int zsize = LZ4_compress_fast_continue(&z->stream, z->buf, z->zbuf + 12,
					       fill, z->zbufSize - 12, 1);
	if (zsize < 1)
	    return ERROR("LZ4_compress_fast_continue", "compression failed"), false;
	// Write the frame, along with the frame header.
	unsigned frameHeader[] = { htole32(0x184D2A57), htole32(zsize + 4), htole32(fill) };
	memcpy(z->zbuf, frameHeader, 12);
	if (!xwrite(out, z->zbuf, 12 + zsize))
	    return ERRNO("write"), false;
	// Update the stats.
	stats->total += fill;
	if (stats->maxSize < fill)
	    stats->maxSize = fill;
	if (stats->zmaxSize < zsize)
	    stats->zmaxSize = zsize;
	if (eof)
	    break;
    }

    return true;
}

int zpkglistCompress(int in, int out, const char *err[2],
		     void (*hash)(void *buf, unsigned size, void *arg), void *arg)
{
    // Verify the output fd.
    struct stat st;
    if (fstat(out, &st) < 0)
	return ERRNO("fstat"), -1;
    if (!S_ISREG(st.st_mode))
	return ERRSTR("output not a regular file"), -1;
    if (st.st_size)
	return ERRSTR("output file not empty"), -1;
    if (lseek(out, 0, SEEK_CUR) != 0)
	return ERRSTR("output file not positioned at the beginning"), -1;

    // Prepare the leading frame.
    unsigned frame[] = { htole32(0x184D2A55), htole32(16), 0, 0, 0, 0 };
    if (!xwrite(out, frame, sizeof frame))
	return ERRNO("write"), -1;

    // Compress.
    struct Z *z = zNew(err);
    if (!z)
	return -1;
    struct stats stats = { 0, 0, 0 };
    if (!zLoop(z, &stats, in, out, err, hash, arg)) {
	zFree(z);
	return -1;
    }
    zFree(z);

    // Empty input?
    if (stats.total == 0)
	return 0;

    // Rewrite the leading frame.
    if (stats.total > ~0U)
	return ERRSTR("output too big"), -1;
    if (lseek(out, 0, SEEK_SET) != 0)
	return ERRNO("lseek"), -1;
    frame[2] = htole32(stats.total);
    frame[4] = htole32(stats.maxSize);
    frame[5] = htole32(stats.zmaxSize);
    if (!xwrite(out, frame, sizeof frame))
	return ERRNO("write"), -1;
    return 1;
}
