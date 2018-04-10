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

#ifndef ZPKGLIST_H
#define ZPKGLIST_H

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

// Compress a list of rpm headers, such as produced by genpkglist,
// into zpkglist file format (see README.md).  Returns 1 on success,
// 0 on empty input (with valid output still written), -1 on error.
// File descriptors and remain open.
// Information about an error is returned via the err[2] parameter:
// the first string is typically a function name, and the second is
// a string which describes the error.  Both strings normally come
// from the read-only data section.
int zpkglistCompress(int in, int out, const char *err[2],
		     void (*hash)(void *buf, unsigned size, void *arg), void *arg)
		     __attribute__((nonnull(3)));

// For decompression, a more general "Reader" API is provided.
// It can also be used to iterate uncompressed header lists.
struct zpkglistReader;
// Returns 1 on success, 0 on EOF at the beginning of input
// (no headers, the Reader handle is not created), -1 on error.
// On success, the Reader handle is returned via zp.
int zpkglistFdopen(struct zpkglistReader **zp, int fd, const char *err[2])
		   __attribute__((nonnull));
// Free without closing.
void zpkglistFree(struct zpkglistReader *z);
// Combines free + close.
void zpkglistClose(struct zpkglistReader *z);

// Bulk reading of uncompressed data into the internal buffer,
// e.g. for checksumming, with optimal buffer management.
// Returns the number of bytes read, 0 on EOF, -1 on error.
// The pointer to the internal buffer is returned via bufp.
// Uncompressed data (such as header magic) is not validated.
// Header boundaries are not preserved.
ssize_t zpkglistBulk(struct zpkglistReader *z, void **bufp,
		     const char *err[2]) __attribute__((nonnull));

// Exposes the inner workings of a blob.
// All integers are in network byte order.
struct HeaderBlob {
    // The number of header entries aka "index length".
    unsigned il;
    // The size of the data segment stored after ee[] aka "data length".
    unsigned dl;
    // Each header entry corresponds to a tag.
    struct HeaderEntry {
	int tag; // e.g. RPMTAG_NAME
	int type; // e.g. RPM_STRING_TYPE or RPM_INT32_TYPE
	unsigned off; // offset into the data segment
	unsigned cnt; // number of elements in array, or 1
    } ee[];
};

#include <stdint.h>

// Read the next header blob, malloc a buffer.
ssize_t zpkglistNextMalloc(struct zpkglistReader *z, struct HeaderBlob **blobp,
	int64_t *posp, const char *err[2]) __attribute__((nonnull(1,2,4)));

// Like NextMalloc except that adds another level of indirection and
// returns the address of the internal pointer to the malloc'd chunk
// with the blob.  To take ownership of the chunk, the caller must nullify
// the pointer.  Otherwise, the chunk will be reused in the next call.
ssize_t zpkglistNextMallocP(struct zpkglistReader *z, struct HeaderBlob ***blobpp,
	int64_t *posp, const char *err[2]) __attribute__((nonnull(1,2,4)));

// Read the next header blob into an internal buffer.
ssize_t zpkglistNextView(struct zpkglistReader *z, struct HeaderBlob **blobp,
	int64_t *posp, const char *err[2]) __attribute__((nonnull(1,2,4)));

#ifdef __cplusplus
}
#endif

#endif
