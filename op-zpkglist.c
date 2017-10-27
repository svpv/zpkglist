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
    return zreader_getFrame(z->opState, bufp, NULL, err);
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

static ssize_t OP(NextView)(struct zpkglistReader *z,
	void **bufp, int64_t *posp, const char *err[2])
{
    if (!z->readState) {
	z->readState = malloc(sizeof(union readState));
	if (!z->readState)
	    return ERRNO("malloc"), -1;
	memset(z->readState, 0, sizeof(union readState));
    }
    struct headerReadState *s = &((union readState *) z->readState)->h;
    if (s->cur == s->end) {
	ssize_t ret = zreader_getFrame(z->opState, (void **) &s->cur, &s->pos, err);
	if (ret <= 0)
	    return ret;
	s->end = s->cur + ret;
	s->ix = 0;
	if (s->end - s->cur < 16)
	    return ERRSTR("bad header size"), -1;
	if (!headerCheckMagic(s->cur))
	    return ERRSTR("bad header magic"), -1;
    }
    ssize_t dataSize = headerDataSize(s->cur);
    if (dataSize < 0 || 16 + dataSize > s->end - s->cur)
	return ERRSTR("bad data size"), -1;
    void *blob = s->cur;
    s->cur += 16 + dataSize;
    if (s->cur != s->end) {
	if (s->end - s->cur < 16)
	    return ERRSTR("bad header size"), -1;
	if (!headerCheckMagic(s->cur))
	    return ERRSTR("bad header magic"), -1;
    }
    if (s->ix > 3)
	return ERRSTR("too many headers in a frame"), -1;
    if (posp)
	*posp = ((int64_t) s->pos << 2) + s->ix;
    s->ix++;
    *bufp = blob + 8;
    return 8 + dataSize;
}

static ssize_t OP(NextMalloc)(struct zpkglistReader *z, int64_t *posp, const char *err[2])
{
    void *view;
    ssize_t ret = OP(NextView)(z, &view, posp, err);
    if (ret <= 0)
	return ret;
    size_t allocSize = 8 + ret + 8;
#ifdef __SSE2__
    allocSize += 24;
#endif
    char *p = generic_opHdrBuf(z, allocSize);
    if (!p)
	return ERRNO("malloc"), -1;
    memcpy(p, view, allocSize);
    return ret;
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
