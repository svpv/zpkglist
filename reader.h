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

#include <stdbool.h>

#pragma GCC visibility push(hidden)

// Uncompressed reader state.
struct aReader {
    int fd;
    bool eof;
    // When a file has just been opened, its leading bytes
    // are stored in the lead[].
    bool justOpened;
    char lead[16];
    // The leading bytes of the next header is typically read and
    // checked ahead of time.  This dataSize applies to the next header.
    size_t dataSize;
    // The buffer.
    char *buf;
    size_t bufSize;
};

// Compressed reader state.
struct zReader {
    int fd;
    bool eof;
    // stats
    unsigned contentSize;
    unsigned maxSize;
    unsigned zmaxSize;
    // Compressed and decompressed buffers,
    // maxSize and zmaxSize bytes.
    char *buf, *zbuf;
    // Keeps the last 8 bytes of the dictionary which are clobbered
    // to and fro with the first header's magic.
    char save[8];
    // The leading fields of a frame to read.
    unsigned lead[3];
};

int aInit(struct aReader *a, int fd, char lead[16], const char *err[2]);
int zInit(struct zReader *z, int fd, char lead[16], const char *err[2]);

union u {
    struct aReader a;
    struct zReader z;
};

struct ops {
    int (*opReadBuf)(union u *u, void **bufp, const char *err[2]);
    void (*opClose)(union u *u);
};

extern struct ops aOps;
extern struct ops zOps;

#pragma GCC visibility pop
