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

#include <stdint.h>
#include <stdbool.h>
#include "reada.h"

#pragma GCC visibility push(hidden)

struct zpkglistReader;

struct ops {
    // Creating stream.
    bool (*opOpen)(struct zpkglistReader *z, const char *err[2]);
    bool (*opReopen)(struct zpkglistReader *z, const char *err[2]);
    void (*opFree)(struct zpkglistReader *z);
    // Basic reading.
    ssize_t (*opRead)(struct zpkglistReader *z, void *buf, size_t size, const char *err[2]);
    // Uncompressed size.
    int64_t (*opContentSize)(struct zpkglistReader *z);
    // Bulk reading, internal buffer.
    ssize_t (*opBulk)(struct zpkglistReader *z, void **bufp, const char *err[2]);
    // Header reading.
    ssize_t (*opHdrSize)(struct zpkglistReader *z, const char *err[2]);
    bool (*opHdrRead)(struct zpkglistReader *z, void *buf, const char *err[2]);
    // Header reading, internal buffer.
    ssize_t (*opHdrReadP)(struct zpkglistReader *z, void **bufp, const char *err[2]);
    // Header position (before read), 0 = EOF or error or not supported.
    unsigned (*opTell)(struct zpkglistReader *z);
    bool (*opSeek)(struct zpkglistReader *z, unsigned pos, const char *err[2]);
};

extern const struct ops
    ops_rpmheader,
    ops_zpkglist,
    ops_lz4,
    ops_zstd,
    ops_xz;

// Read the uncompress byte stream.  Concatenates frames.
ssize_t zread(struct zpkglistReader *z, void *buf, size_t size, const char *err[2]);

// Generic opBulk implementation, uses someting like zread, fills z->bulkBuf.
ssize_t generic_opBulk(struct zpkglistReader *z, void **bufp, const char *err[2]);

// Generic opRead implemented in terms of opBulk, uses z->readState.
ssize_t generic_opRead(struct zpkglistReader *z, void *buf, size_t size, const char *err[2]);

struct zpkglistReader {
    struct fda fda;
    char fdabuf[NREADA];
    const struct ops *ops;
    void *opState;
    union {
	void *bulkBuf;
	void *readState;
    };
};

#define CAT_(x, y) x ## y
#define CAT2(x, y) CAT_(x, y)
#define CAT3(x, y, z) CAT2(x, CAT2(y, z))

#pragma GCC visibility pop
