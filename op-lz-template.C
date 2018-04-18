#ifndef OP_LZ_TEMPLATE_C
#define OP_LZ_TEMPLATE_C

#define OP(op) CAT3(LZ, _op, op)
#define LZREADER CAT2(LZ, reader)
#define CALL(method) CAT3(LZREADER, _, method)
#define OPS CAT2(ops_, LZ)

#include "magic4.h"
#define MAGIC4_W_xz MAGIC4_W_XZ
#define MAGIC4_W_zstd MAGIC4_W_ZSTD
#define MAGIC4_W_LZ CAT2(MAGIC4_W_, LZ)

#endif

static bool OP(Open)(struct zpkglistReader *z, const char *err[2])
{
    struct LZREADER *LZ;
    int rc = CALL(open)(&LZ, &z->fda, err);
    if (rc < 0)
	return false;
    assert(rc > 0); // starts with the magic
    z->hasLead = false;
    return z->reader = LZ, true;
}

static bool OP(Reopen)(struct zpkglistReader *z, const char *err[2])
{
    int rc = CALL(reopen)(z->reader, &z->fda, err);
    if (rc < 0)
	return false;
    assert(rc > 0); // starts with the magic
    z->hasLead = false;
    return true;
}

static void OP(Free)(struct zpkglistReader *z)
{
    CALL(free)(z->reader);
}

static ssize_t OP(Read)(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    size_t total = 0;
    while (1) {
	ssize_t n = CALL(read)(z->reader, buf, size, err);
	if (n < 0)
	    return -1;
	total += n;
	if (n == size)
	    return total;
	assert(n < size);
	buf = (char *) buf + n, size -= n;
	// Got n < size, trying to concatenate.
	unsigned w;
	ssize_t ret = peeka(&z->fda, &w, 4);
	if (ret < 0)
	    return ERRNO("read"), -1;
	if (ret == 0)
	    return total;
	if (ret != 4)
	    return ERRSTR("unexpected EOF"), -1;
	if (w != MAGIC4_W_LZ) // The caller can recognize end-of-stream, because
	    return total;     // the number of bytes read is less than requested.
	int rc = CALL(reopen)(z->reader, &z->fda, err);
	if (rc < 0)
	    return -1;
	assert(rc > 0);
    }
}

static int64_t OP(ContentSize)(struct zpkglistReader *z)
{
#ifdef CONTENTSIZE
    return CALL(contentSize)(z->reader);
#endif
    return -1;
}

const struct ops OPS = {
    OP(Open),
    OP(Reopen),
    OP(Free),
    OP(Read),
    OP(ContentSize),
    lz_opBulk,
    lz_opNextMalloc,
    lz_opNextView,
};
