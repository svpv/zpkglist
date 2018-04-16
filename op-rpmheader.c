#include "error.h"
#include "header.h"
#include "reader.h"
#include "reada.h"

#define OP(op) CAT2(rpmheader_op, op)
#define OPS ops_rpmheader

static bool OP(Open)(struct zpkglistReader *z, const char *err[2])
{
    z->left = 0;
    return true;
}

static bool OP(Reopen)(struct zpkglistReader *z, const char *err[2])
{
    z->left = 0;
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

static ssize_t OP(Bulk)(struct zpkglistReader *z, void **bufp, const char *err[2])
{
    // Prefill the buffer.
    unsigned lead[4];
    ssize_t ret = peeka(&z->fda, lead, 16);
    if (ret < 0)
	return ERRNO("read"), -1;

    // How much to grab from the buffer?
    size_t grab;
    // Are we in the middle of a header?
    if (z->left) {
	if (ret == 0)
	    return ERRSTR("unexpected EOF"), -1;
	// Grab the rest of the header.
	grab = z->left;
    }
    else {
	// Header boundary, check the magic etc.
	if (ret == 0)
	    return 0;
	if (ret < 16)
	    return ERRSTR("unexpected EOF"), -1;
	if (!headerCheckMagic(lead))
	    return ERRSTR("bad header magic"), -1; // XXX eos
	ssize_t dataSize = headerDataSize(lead);
	if (dataSize < 0)
	    return ERRSTR("bad header size"), -1;
	// Would like to grab the whole header.
	grab = 16 + dataSize;
    }

    // Have only this much in the buffer.
    size_t fill = z->fda.end - z->fda.cur;

    // Can I grab even more?  Try to peek at the next header.
    while (grab + 16 <= fill) {
	memcpy(lead, z->fda.cur + grab, 16);
	if (!headerCheckMagic(lead))
	    return ERRSTR("bad header magic"), -1; // XXX eos
	ssize_t dataSize = headerDataSize(lead);
	if (dataSize < 0)
	    return ERRSTR("bad header size"), -1;
	grab += 16 + dataSize;
    }

    *bufp = z->fda.cur;

    if (grab > fill) {
	z->left = grab - fill;
	z->fda.cur = z->fda.end = NULL;
	return fill;
    }

    z->left = 0;
    z->fda.cur += grab;
    return grab;
}

static ssize_t OP(NextMalloc)(struct zpkglistReader *z, int64_t *posp, const char *err[2])
{
    off_t pos = tella(&z->fda) - 16 * z->hasLead;
    ssize_t ret = generic_opNextMalloc(z, err);
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
    OP(Bulk),
    OP(NextMalloc),
};
