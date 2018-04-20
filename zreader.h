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
#include <sys/types.h> // ssize_t

#pragma GCC visibility push(hidden)

struct fda; // reada.h
struct zreader;

// A lower-level API for reading zpkglist files, similar to lz4reader.
int zreader_open(struct zreader **zp, struct fda *fda, const char *err[2])
		 __attribute__((nonnull));

void zreader_free(struct zreader *z);

// Read the next frame with up to 4 header blobs.
// Pointer to an internal buffer is returned via bufp.  If the mallocJumbo
// mode is enabled, jumbo frames (unlike normal frames) will be malloc'd,
// and ownership over the malloc'd chunk is transfered to the caller.
// The situation is signaled by returning the negative size, ret < 128K.
// There are no magic bytes with the first header; however, with ret > 0,
// the magic is implicitly prepended to the buffer (starting at *bufp - 8;
// such prepending is obviously not possible with malloc'd chunks).
ssize_t zreader_getFrame(struct zreader *z, void **bufp, off_t *posp,
			 bool mallocJumbo, const char *err[2])
			 __attribute__((nonnull(1,2,5)));

unsigned zreader_contentSize(struct zreader *z) __attribute__((nonnull));

#pragma GCC visibility pop
