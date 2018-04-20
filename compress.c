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
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <endian.h>
#include <lz4.h>
#include "zpkglist.h"
#include "error.h"
#include "xwrite.h"
#include "header.h"
#include "train/rpmhdrzdict.h"

struct Z {
    // The clean LZ4 state is initialized only once.  (This is a relatively
    // expensive step, because the dictionary has to be scanned and hashed.)
    // When compressing each frame, the clean state is quickly cloned.
    // LZ4_stream_t internals are not accessed, yet we assume that the size
    // of LZ4_stream_t will remain the same in future LZ4 releases (and indeed
    // the LZ4 library makes some provision to ensure that the size won't
    // change, namely it uses "union LZ4_stream_u" to reserve some space).
    LZ4_stream_t stream0, stream;
    // The dictionary, must be adjacent to the input data.
    char dict[64<<10];
    // The input buffer (which contains a few rpm header blobs) and the output
    // buffer (with compressed LZ4 data).
    char buf[(128<<10)+LZ4_COMPRESSBOUND(128<<10)];
};

ssize_t zpkglistCompress(int infd, int outfd,
			 void (*hash)(const void *buf, size_t size, void *arg),
			 void *arg, const char *err[2])
{
    // Get the initial file position, will seek back.
    off_t pos0 = lseek(outfd, 0, SEEK_CUR);
    if (pos0 < 0)
	return ERRNO("lseek"), -1;

    // Prepare the leading frame.
    struct {
	unsigned magic;
	unsigned size16;
	uint64_t total;
	unsigned buf1size;
	unsigned jbufsize;
    } frame0 = { htole32(0x184D2A55), htole32(16), 0, 0, 0 };

    // Write the leading frame, to be rewritten later.
    if (!xwrite(outfd, &frame0, sizeof frame0))
	return ERRNO("write"), -1;

    // Open the input.
    struct zpkglistReader *zin;
    int rc = zpkglistFdopen(&zin, infd, err);
    if (rc <= 0)
	return rc;

    // The input has been opened, and must be closed upon return.  I understand
    // C++ can overload operators, but can it overload operator return?
#define freez (void)0
#define freebuf (void)0
#define freezbuf (void)0
#define return return zpkglistFree(zin), freez, freebuf, freezbuf,

    // Load the leading bytes of the first header: 8 magic + 8 (il,dl).
    unsigned lead[4];
    ssize_t ret = zpkglistRead(zin, lead, 16, err);
    // If it's EOF or an error, do nothing.
    if (ret <= 0)
	return ret;
    if (ret < 16)
	return ERRSTR("unexpected EOF"), -1;
    if (!headerCheckMagic(lead))
	return ERRSTR("bad header magic"), -1;

    // The size of the header's data after (il,dl).
    ssize_t dataSize = headerDataSize(lead);
    if (dataSize < 0)
	return ERRSTR("bad header size"), -1;

    // Write the dictionary frame.
    if (!xwrite(outfd, rpmhdrzdict, sizeof rpmhdrzdict))
	return ERRNO("write"), -1;

    // Set buf1size to zdict size (not including the frame header).
    frame0.buf1size = sizeof rpmhdrzdict - 8;

    // Allocate and initialize the compressor state.
    struct Z *z = malloc(sizeof *z);
    if (!z)
	return ERRNO("malloc"), -1;
    // Uncompress the dictionary into z->dict.
    int zret = LZ4_decompress_fast(rpmhdrzdict + 8, z->dict, sizeof z->dict);
    assert(zret == sizeof rpmhdrzdict - 8);
    // Initialize the clean state.
    memset(&z->stream0, 0, sizeof z->stream0);
    // Load the dictionary into the clean state.
    zret = LZ4_loadDict(&z->stream0, z->dict, sizeof z->dict);
    assert(zret == sizeof z->dict);

    // Or can C++ overload operators twice in the same scope?
    // Or can it draw out Leviathan with an hook?
#undef freez
#define freez free(z)

    // The number of headers processed (the return value).
    size_t nhdr = 0;

    while (1) {
	// Jumbo frame?
	if (8 + dataSize > (128<<10)) {
	    // Input+output won't fit into z->buf.  Try to reuse
	    // z->buf just for the input.  Need 16 more bytes to peek
	    // at the next header.  The leading magic won't be written,
	    // but may need to restore it for hashing the original data.
	    char *buf = z->buf;
	    if (8 + dataSize + 16 > sizeof z->buf) {
		buf = malloc(16 + dataSize + 16);
		if (!buf)
		    return ERRNO("malloc"), -1;
		memset(buf, 0, 8);
		buf += 8;
	    }
#undef freebuf
#define freebuf (buf == z->buf ? (void)0 : free(buf - 8))

	    // Fill the input buffer.
	    memcpy(buf, lead + 2, 8);
	    ret = zpkglistRead(zin, buf + 8, dataSize + 16, err);
	    if (ret < 0)
		return -1;

	    bool eof = false;
	    if (ret == dataSize)
		eof = true;
	    else if (ret != dataSize + 16)
		return ERRSTR("unexpected EOF"), -1;
	    else {
		// Save the next header's leading bytes for the next iteration.
		memcpy(lead, buf + 8 + dataSize, 16);
		// Verify the next header's magic - otherwise, we aren't even sure
		// we got the right size.
		if (!headerCheckMagic(lead))
		    return ERRSTR("bad header magic"), -1;
	    }

	    // Hash the data.  The leading magic is restored by clobbering
	    // to and fro the last eight bytes of the dictionary.
	    if (hash) {
		char save[8];
		char *pre = buf - 8;
		memcpy(save, pre, 8);
		memcpy(pre, headerMagic, 8);
		hash(pre, 16 + dataSize, arg);
		memcpy(pre, save, 8);
	    }

	    // Allocate the output buffer.  Need 12 extra bytes for the frame header.
	    size_t zbufSize = LZ4_COMPRESSBOUND(8 + dataSize);
	    char *zbuf = malloc(12 + zbufSize);
	    if (!zbuf)
		return ERRNO("malloc"), -1;
	    zbuf += 12;
#undef freezbuf
#define freezbuf free(zbuf - 12)

	    // Compress, without dictionary.
	    int zsize = LZ4_compress_fast(buf, zbuf, 8 + dataSize, zbufSize, 1);

	    // Input buffer no longer needed.
	    freebuf;
#undef freebuf
#define freebuf (void)0

	    if (zsize < 1)
		return ERROR("LZ4_compress_fast", "compression failed"), -1;

	    // Prepend the frame header.
	    unsigned frameHeader[] = {
		htole32(0x184D2A57),
		htole32(zsize + 4), // compressed size + 4, as per the spec
		htole32(8 + dataSize),
	    };
	    memcpy(zbuf - 12, frameHeader, 12);

	    // Write the output.
	    bool written = xwrite(outfd, zbuf - 12, 12 + zsize);

	    // Free the output buffer.
	    freezbuf;
#undef freezbuf
#define freezbuf (void)0

	    if (!written)
		return ERRNO("write"), -1;

	    // Update the stats.
	    frame0.total += 16 + dataSize; // including the magic
	    if (frame0.buf1size < zsize)
		frame0.buf1size = zsize;
	    if (frame0.jbufsize < 8 + dataSize)
		frame0.jbufsize = 8 + dataSize;
	    nhdr++;

	    // Concatenate the next frame?
	    if (eof) {
		ret = zpkglistRead(zin, lead, 16, err);
		if (ret < 0)
		    return -1;
		if (ret == 0)
		    break; // true EOF
		if (ret < 16)
		    return ERRSTR("unexpected EOF"), -1;
		if (!headerCheckMagic(lead))
		    return ERRSTR("bad header magic"), -1;
	    }

	    // The next header is for the next iteration.
	    dataSize = headerDataSize(lead);
	    if (dataSize < 0)
		return ERRSTR("bad header size"), -1;

	    // Kind of a separate loop, hard to factor common code.
	    continue;
	}

	// Gonna try to fit four headers into 128K.
	char *cur = z->buf;
	bool eof = false;

	// Iterate input headers, append to cur.
	// On each iteration, we know that the header fits in.
	for (int i = 0; i < 4; i++) {
	    nhdr++;
	    // Put this header's leading bytes.
	    // The very first magic won't be written.
	    if (i == 0) {
		memcpy(cur, lead + 2, 8);
		cur += 8;
	    }
	    else {
		// Otherwise, gonna put 16 + dataSize bytes.
		memcpy(cur, lead, 16);
		cur += 16;
	    }

	    // Read this header's data + the next header's leading bytes.
	    ret = zpkglistRead(zin, cur, dataSize + 16, err);
	    if (ret < 0)
		return -1;
	    cur += dataSize;
	    // Concatenate the next frame?
	    if (ret == dataSize) {
		// No need to append, read directly into lead[].
		ret = zpkglistRead(zin, lead, 16, err);
		if (ret < 0)
		    return -1;
		if (ret == 0) {
		    eof = true; // true EOF
		    break;
		}
		if (ret < 16)
		    return ERRSTR("unexpected EOF"), -1;
		if (!headerCheckMagic(lead))
		    return ERRSTR("bad header magic"), -1;
	    }
	    else if (ret == dataSize + 16)
		// Save the next header's leading bytes for the next iteration.
		memcpy(lead, cur, 16);
	    else
		return ERRSTR("unexpected EOF"), -1;

	    // Verify the next header's magic - otherwise, we aren't even sure
	    // we got the right size.
	    if (!headerCheckMagic(lead))
		return ERRSTR("bad header magic"), -1;

	    // The next header is for the next iteration - either in this
	    // "for i" loop, or in the outer "while 1" loop.
	    dataSize = headerDataSize(lead);
	    if (dataSize < 0)
		return ERRSTR("bad header size"), -1;

	    // So the header has been appended to cur, which now points to the
	    // end of the buffer, and the only question remains, does the next
	    // header still fit in?  If it doesn't, break out early.
	    // Otherwise, rely on the loop control.
	    if ((cur - z->buf) + (16 + dataSize) > (128 << 10))
		break;
	}

	// Hash the data.
	size_t fill = cur - z->buf;
	if (hash) {
	    char save[8];
	    char *pre = z->buf - 8;
	    memcpy(save, pre, 8);
	    memcpy(pre, headerMagic, 8);
	    hash(pre, fill + 8, arg);
	    memcpy(pre, save, 8);
	}

	// Copy the clean state (struct assignment).
	z->stream = z->stream0;

	// Set up the output buffer right after the input buffer.
	char *zbuf = z->buf + fill;
	size_t zbufSize = sizeof z->buf - fill;
	assert(zbufSize >= LZ4_COMPRESSBOUND(fill));

	// Compress the frame.
	int zsize = LZ4_compress_fast_continue(&z->stream, z->buf, zbuf, fill, zbufSize, 1);
	if (zsize < 1)
	    return ERROR("LZ4_compress_fast_continue", "compression failed"), -1;

	// Write the frame, along with the frame header.
	unsigned frameHeader[] = {
	    htole32(0x184D2A57),
	    htole32(zsize + 4),
	    htole32(fill),
	};
	// Clobbers uncompressed input.
	memcpy(zbuf - 12, frameHeader, 12);
	if (!xwrite(outfd, zbuf - 12, 12 + zsize))
	    return ERRNO("write"), -1;

	// Update the stats.
	frame0.total += 8 + fill; // including the magic
	if (frame0.buf1size < fill + zsize)
	    frame0.buf1size = fill + zsize;

	if (eof)
	    break;
    }

    // Rewrite the leading frame.
    frame0.total = htole64(frame0.total);
    frame0.buf1size = htole32(frame0.buf1size);
    frame0.jbufsize = htole32(frame0.jbufsize);
    if (lseek(outfd, pos0, SEEK_SET) != 0)
	return ERRNO("lseek"), -1;
    if (!xwrite(outfd, &frame0, sizeof frame0))
	return ERRNO("write"), -1;

    // God knows how hard it is to trigger this assetion.
    assert(nhdr > 0 && nhdr < SSIZE_MAX);
    return nhdr;
}
