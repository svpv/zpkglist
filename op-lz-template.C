#ifndef OP_LZ_TEMPLATE_C
#define OP_LZ_TEMPLATE_C

#define CAT_(x, y) x ## y
#define CAT2(x, y) CAT_(x, y)
#define CAT3(x, y, z) CAT2(x, CAT2(y, z))

#define OP(op) CAT3(LZ, _op, op)
#define LZREADER CAT2(LZ, reader)
#define CALL(method) CAT3(LZREADER, _, method)
#define OPS CAT2(ops_, LZ)

#endif

static bool OP(Open)(struct zpkglistReader *z, const char *err[2])
{
    struct LZREADER *LZ;
    int rc = CALL(open)(&LZ, &z->fda, err);
    if (rc < 0)
	return false;
    assert(rc > 0); // starts with the magic
    return z->opState = LZ, true;
}

static bool OP(Reopen)(struct zpkglistReader *z, const char *err[2])
{
    int rc = CALL(reopen)(z->opState, &z->fda, err);
    if (rc < 0)
	return false;
    assert(rc > 0); // starts with the magic
    return true;
}

static void OP(Free)(struct zpkglistReader *z)
{
    CALL(free)(z->opState);
}

static ssize_t OP(Read)(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    return CALL(read)(z->opState, buf, size, err);
}

static int64_t OP(ContentSize)(struct zpkglistReader *z)
{
#ifdef CONTENTSIZE
    return CALL(contentSize)(z->opState);
#endif
    return -1;
}

const struct ops OPS = {
    OP(Open),
    OP(Reopen),
    OP(Free),
    OP(Read),
    OP(ContentSize),
};
