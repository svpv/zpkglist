#include <string.h>
#include <stdlib.h>

#include "zstdreader.h"
#include "xzreader.h"

#include "error.h"
#include "header.h"
#include "reader.h"

static ssize_t lz_opBulk(struct zpkglistReader *z, void **bufp, const char *err[2])
{
    // Zstd compresses data in 128K blocks.
    size_t bulkSize = 128 << 10;
    if (!z->buf)
	z->buf = malloc(z->bufSize = bulkSize);
    else if (z->bufSize < bulkSize) {
	free(z->buf);
	z->buf = malloc(z->bufSize = bulkSize);
    }
    if (!z->buf)
	return ERRNO("malloc"), -1;

    // Check against header reading.
    assert(!z->hasLead);

    ssize_t n = z->ops->opRead(z, z->buf, bulkSize, err);
    if (n > 0)
	*bufp = z->buf;
    return n;
}

// Reallocate z->buf for opNextMalloc.
void *generic_opHdrBuf(struct zpkglistReader *z, size_t size)
{
    // For the first time, allocate the exact size.
    // Roudning up only helps with reallocs.
    if (!z->buf)
	return z->buf = malloc(z->bufSize = size);
    // We have the buffer, so this is the second-time logic.
    // Adjacent header blobs differ in size only by a few hundred bytes,
    // on average.  A modest bump of the size reduces the number of malloc
    // calls by a factor of 1.5.
    if (z->bufSize < size) {
	free(z->buf);
	size = (size + 1536) & ~1023;
	return z->buf = malloc(z->bufSize = size);
    }
    // If the buffer's somewhat big, maybe try to switch to a smaller one.
    // For bloated headers with changelogs, we have the following quantiles:
    // 75% - 7K, 90% - 16K, 95% - 26K, 99% - 79K.
    size = (size + 16384) & ~1023;
    // Anyway, lz_opBulk allocates 128K, so don't shrink below 128K.
    if (z->bufSize > (128<<10) && z->bufSize > 2 * size) {
	free(z->buf);
	return z->buf = malloc(z->bufSize = size);
    }
    // The existing buffer is okay.
    return z->buf;
}

static ssize_t lz_opNextMalloc(struct zpkglistReader *z, int64_t *posp, const char *err[2])
{
    if (!z->hasLead) {
	ssize_t ret = z->ops->opRead(z, z->lead, 16, err);
	if (ret <= 0)
	    return ret;
	if (ret < 16)
	    return ERRSTR("unexpected EOF"), -1;
	if (!headerCheckMagic(z->lead))
	    return ERRSTR("bad header magic"), -1;
	z->hasLead = true;
    }
    ssize_t dataSize = headerDataSize(z->lead);
    if (dataSize < 0)
	return ERRSTR("bad header size"), -1;

    char *buf = generic_opHdrBuf(z, 8 + dataSize + 16);
    if (!buf)
	return ERRNO("malloc"), -1;

    memcpy(buf, z->lead + 2, 8);

    ssize_t ret = z->ops->opRead(z, buf + 8, dataSize + 16, err);
    if (ret == dataSize + 16) {
	memcpy(z->lead, buf + 8 + dataSize, 16);
	if (!headerCheckMagic(z->lead))
	    return ERRSTR("bad header magic"), -1;
    }
    else if (ret == dataSize) {
	z->hasLead = false;
	// Re-add the trailing 16 bytes characteristic of op-lz.c chunks.
	memcpy(buf + 8 + dataSize, z->lead, 16);
    }
    else
	return ERRSTR("unexpected EOF"), -1;

    // File position not supported.  Cannot get back, at least.
    if (posp)
	*posp = -1;
    return 8 + dataSize;
}

static ssize_t lz_opNextView(struct zpkglistReader *z, void **blobp, int64_t *posp, const char *err[2])
{
    ssize_t ret = lz_opNextMalloc(z, posp, err);
    if (ret > 0)
	*blobp = z->buf;
    return ret;
}

// Zstd supports contentSize.
#define CONTENTSIZE

#define LZ zstd
#include "op-lz-template.C"
#undef LZ

// XZ does not support contentSize.
#undef CONTENTSIZE

#define LZ xz
#include "op-lz-template.C"
#undef LZ
