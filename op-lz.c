#include <string.h>
#include <stdlib.h>

#include "lz4reader.h"
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
    }
    else if (ret == dataSize)
	z->eof = true;
    else
	return ERRSTR("unexpected EOF"), false;
    return true;
}

ssize_t generic_opNextMalloc(struct zpkglistReader *z,
	void **bufp, bool needMagic, const char *err[2])
{
    ssize_t dataSize = generic_opNextSize(z, err);
    if (dataSize <= 0)
	return dataSize;
    char *p = malloc(8 * needMagic + 8 + dataSize + 16);
    if (!p)
	return ERRNO("malloc"), -1;
    if (needMagic)
	memcpy(p, z->lead, 16);
    else
	memcpy(p, z->lead + 8, 8);
    if (!generic_opNextRead(z, p + 8 * needMagic + 8, dataSize, err))
	return free(p), -1;
    *bufp = p;
    return 8 * needMagic + 8 + dataSize;
}

ssize_t generic_opNextView(struct zpkglistReader *z,
	void **bufp, bool needMagic, const char *err[2])
{
    ssize_t dataSize = generic_opNextSize(z, err);
    if (dataSize <= 0)
	return dataSize;
    char *p;
    size_t needBytes = 8 * needMagic + 8 + dataSize + 16;
    if (needBytes <= sizeof z->hdrSmallBuf)
	p = z->hdrSmallBuf,
	free(z->hdrBuf), z->hdrBuf = NULL, z->hdrBufSize = 0;
    else if (needBytes <= z->hdrBufSize)
	p = z->hdrBuf;
    else {
	free(z->hdrBuf);
	p = z->hdrBuf = malloc(needBytes);
	if (!p) {
	    z->hdrBufSize = 0;
	    return ERRNO("malloc"), -1;
	}
	z->hdrBufSize = needBytes;
    }
    if (needMagic)
	memcpy(p, z->lead, 16);
    else
	memcpy(p, z->lead + 8, 8);
    if (!generic_opNextRead(z, p + 8 * needMagic + 8, dataSize, err))
	return -1;
    *bufp = p;
    return 8 * needMagic + 8 + dataSize;
}

static ssize_t lz_opNextMalloc(struct zpkglistReader *z,
	void **bufp, int64_t *posp, bool needMagic, const char *err[2])
{
    ssize_t ret = generic_opNextMalloc(z, bufp, needMagic, err);
    // File position not supported.  Cannot get back, at least.
    if (ret > 0 && posp)
	*posp = -1;
    return ret;
}

static ssize_t lz_opNextView(struct zpkglistReader *z,
	void **bufp, int64_t *posp, bool needMagic, const char *err[2])
{
    ssize_t ret = generic_opNextView(z, bufp, needMagic, err);
    if (ret > 0 && posp)
	*posp = -1;
    return ret;
}

// LZ4 and zstd support contentSize.
#define CONTENTSIZE

#define LZ lz4
#include "op-lz-template.C"
#undef LZ

#define LZ zstd
#include "op-lz-template.C"
#undef LZ

// XZ does not support contentSize.
#undef CONTENTSIZE

#define LZ xz
#include "op-lz-template.C"
#undef LZ
