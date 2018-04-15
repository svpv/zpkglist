#include <string.h>
#include <stdlib.h>

#include "zstdreader.h"
#include "xzreader.h"

#include "error.h"
#include "header.h"
#include "reader.h"

static ssize_t generic_opNextSize(struct zpkglistReader *z, const char *err[2])
{
    if (z->eof)
	return 0;
    if (!z->hasLead) {
	ssize_t ret = zread(z, z->lead, 16, err);
	if (ret < 0)
	    return -1;
	if (ret == 0) {
	    z->eof = true;
	    return 0;
	}
	if (ret < 16)
	    return ERRSTR("unexpected EOF"), -1;
	if (!headerCheckMagic(z->lead))
	    return ERRSTR("bad header magic"), -1;
	z->hasLead = true;
    }
    ssize_t dataSize = headerDataSize(z->lead);
    if (dataSize < 0)
	return ERRSTR("bad header size"), -1;
    return dataSize;
}

static bool generic_opNextRead(struct zpkglistReader *z,
	char *buf, size_t dataSize, const char *err[2])
{
    ssize_t ret = zread(z, buf, dataSize + 16, err);
    if (ret == dataSize + 16) {
	memcpy(z->lead, buf + dataSize, 16);
	if (!headerCheckMagic(z->lead))
	    return ERRSTR("bad header magic"), false;
#ifdef __SSE2__
	memset(buf + dataSize + 16, 0, 16);
#endif
    }
    else if (ret == dataSize) {
	z->eof = true;
	memcpy(buf + dataSize, headerMagic, 8);
#ifdef __SSE2__
	memset(buf + dataSize + 8, 0, 24);
#endif
    }
    else
	return ERRSTR("unexpected EOF"), false;
    return true;
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
    if (z->bufSize > (80<<10) && z->bufSize > 2 * size) {
	free(z->buf);
	return z->buf = malloc(z->bufSize = size);
    }
    // The existing buffer is okay.
    return z->buf;
}

ssize_t generic_opNextMalloc(struct zpkglistReader *z, const char *err[2])
{
    ssize_t dataSize = generic_opNextSize(z, err);
    if (dataSize <= 0)
	return dataSize;
    size_t allocSize = 8 + dataSize + 16;
#ifdef __SSE2__
    allocSize += 16;
#endif
    char *p = generic_opHdrBuf(z, allocSize);
    if (!p)
	return ERRNO("malloc"), -1;
    memcpy(p, z->lead + 2, 8);
    if (!generic_opNextRead(z, p + 8, dataSize, err))
	return -1;
    return 8 + dataSize;
}

static ssize_t lz_opNextMalloc(struct zpkglistReader *z, int64_t *posp, const char *err[2])
{
    ssize_t ret = generic_opNextMalloc(z, err);
    // File position not supported.  Cannot get back, at least.
    if (ret > 0 && posp)
	*posp = -1;
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
