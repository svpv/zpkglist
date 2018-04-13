#include <stdlib.h>
#include "reader.h"
#include "error.h"
#include "header.h"
#include "zreader.h"

#define OP(op) CAT2(zpkg_op, op)
#define OPS ops_zpkglist

struct byteReadState {
    char *cur;
    char *end;
};

struct headerReadState {
    char *cur;
    char *end;
    off_t pos;
    int ix;
};

union readState {
    struct byteReadState b;
    struct headerReadState h;
};

static ssize_t OP(Bulk)(struct zpkglistReader *z, void **bufp, const char *err[2])
{
    ssize_t ret = zreader_getFrame(z->opState, bufp, NULL, false, err);
    // Materialize the imlicit magic bytes.
    if (ret > 0)
	ret += 8, *bufp -= 8;
    return ret;
}

static ssize_t OP(Read)(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    if (!z->readState) {
	z->readState = malloc(sizeof(union readState));
	if (!z->readState)
	    return ERRNO("malloc"), -1;
	memset(z->readState, 0, sizeof(union readState));
    }
    struct byteReadState *s = &((union readState *) z->readState)->b;
    size_t total = 0;
    do {
	size_t left = s->end - s->cur;
	if (left == 0) {
	    ssize_t ret = OP(Bulk)(z, (void **) &s->cur, err);
	    if (ret < 0)
		return -1;
	    if (ret == 0)
		break;
	    s->end = s->cur + ret;
	    left = ret;
	}
	size_t n = size < left ? size : left;
	memcpy(buf, s->cur, n);
	size -= n, buf = (char *) buf + n;
	s->cur += n;
	total += n;
    } while (size);
    return total;
}

static bool OP(Open)(struct zpkglistReader *z, const char *err[2])
{
    struct zreader *zz;
    int rc = zreader_open(&zz, &z->fda, err);
    if (rc < 0)
	return false;
    assert(rc > 0); // starts with the magic
    return z->opState = zz, true;
}

static bool OP(Reopen)(struct zpkglistReader *z, const char *err[2])
{
    zreader_free(z->opState), z->opState = NULL;
    return OP(Open)(z, err);
}

static void OP(Free)(struct zpkglistReader *z)
{
    zreader_free(z->opState);
}

static int64_t OP(ContentSize)(struct zpkglistReader *z)
{
    return zreader_contentSize(z->opState);
}

static ssize_t OP(NextMalloc)(struct zpkglistReader *z, int64_t *posp, const char *err[2])
{
    if (!z->readState) {
	z->readState = malloc(sizeof(union readState));
	if (!z->readState)
	    return ERRNO("malloc"), -1;
	memset(z->readState, 0, sizeof(union readState));
    }
    bool jumbo = false;
    struct headerReadState *s = &((union readState *) z->readState)->h;
    if (s->cur == s->end) {
	ssize_t ret = zreader_getFrame(z->opState, (void **) &s->cur, &s->pos, true, err);
	if (ret == 0 || ret == -1)
	    return ret;
	// Jumbo frame?
	if (ret < 0)
	    ret = -ret, jumbo = true;
	if (ret < 8)
	    return ERRSTR("bad header size"), -1;
	s->end = s->cur + ret;
	s->ix = 0;
    }
    // The magic has been checked, s->cur points at <il,dl>.
    // There is at least 8 bytes, to the probe for <il,dl> is valid.
    ssize_t dataSize = headerDataSize(s->cur - 8);
    if (dataSize < 0 || 8 + dataSize > s->end - s->cur)
	return ERRSTR("bad data size"), -1;
    void *blob = s->cur;
    s->cur += 8 + dataSize;
    // Is s->cur pointing to the end of the frame?
    if (s->cur != s->end) {
	// Nope, it must be the next magic.
	if (jumbo) {
	    // Jumbo frames must contain only one header.
	    free(blob), s->cur = s->end = NULL;
	    return ERRSTR("bad jumbo size"), -1;
	}
	if (s->end - s->cur < 16)
	    return ERRSTR("bad header size"), -1;
	if (!headerCheckMagic(s->cur))
	    return ERRSTR("bad header magic"), -1;
	s->cur += 8;
    }
    // Combine file offset + header no in a frame => logical offset.
    if (s->ix > 3)
	return ERRSTR("too many headers in a frame"), -1;
    if (posp)
	*posp = ((int64_t) s->pos << 2) + s->ix;
    s->ix++;
    // Jumbo already malloc'd, otherwise pass down to generic allocator.
    if (jumbo) {
	free(z->hdrBuf);
	z->hdrBuf = blob;
	z->hdrBufSize = 8 + dataSize;
    }
    else {
	char *p = generic_opHdrBuf(z, 8 + dataSize);
	if (!p)
	    return ERRNO("malloc"), -1;
	memcpy(p, blob, 8 + dataSize);
    }
    return 8 + dataSize;
}

const struct ops OPS = {
    OP(Open),
    OP(Reopen),
    OP(Free),
    OP(Read),
    OP(ContentSize),
    OP(Bulk),
    OP(NextMalloc),
};
