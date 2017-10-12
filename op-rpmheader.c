#include "reader.h"
#include "reada.h"

#define OP(op) CAT2(rpmheader_op, op)
#define OPS ops_rpmheader

static bool OP(Open)(struct zpkglistReader *z, const char *err[2])
{
    return true;
}

static bool OP(Reopen)(struct zpkglistReader *z, const char *err[2])
{
    return true;
}

static void OP(Free)(struct zpkglistReader *z)
{
}

static ssize_t OP(Read)(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    return reada(&z->fda, buf, size);
}

static int64_t OP(ContentSize)(struct zpkglistReader *z)
{
    return -1;
}

static ssize_t OP(NextMalloc)(struct zpkglistReader *z,
	void **bufp, int64_t *posp, bool needMagic, const char *err[2])
{
    off_t pos = tella(&z->fda) - 16 * z->hasLead;
    ssize_t ret = generic_opNextMalloc(z, bufp, needMagic, err);
    if (ret > 0 && posp)
	*posp = pos;
    return ret;
}

static ssize_t OP(NextView)(struct zpkglistReader *z,
	void **bufp, int64_t *posp, bool needMagic, const char *err[2])
{
    off_t pos = tella(&z->fda) - 16 * z->hasLead;
    ssize_t ret = generic_opNextView(z, bufp, needMagic, err);
    if (ret > 0 && posp)
	*posp = pos;
    return ret;
}

const struct ops OPS = {
    OP(Open),
    OP(Reopen),
    OP(Free),
    OP(Read),
    OP(ContentSize),
    generic_opBulk,
    OP(NextMalloc),
    OP(NextView),
};
