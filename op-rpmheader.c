#include "error.h"
#include "header.h"
#include "reader.h"
#include "reada.h"

#define OP(op) CAT2(rpmheader_op, op)
#define OPS ops_rpmheader

static bool OP(Open)(struct zpkglistReader *z, const char *err[2])
{
    z->left = 0;
    z->hasLead = false;
    return true;
}

static void OP(Free)(struct zpkglistReader *z)
{
}

static ssize_t OP(Read)(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    ssize_t ret;
    size_t total = 0;

    // No bytes left on behalf of the current/last header?
    // Peek at the next header, find out how many bytes can be slurped.
    if (z->left == 0) {
	unsigned lead[16];
    peek:
	ret = peeka(&z->fda, lead, 16);
	if (ret < 0)
	    return ERRNO("read"), -1;
	if (ret == 0)
	    return total;
	if (ret < 16)
	    return ERRSTR("unexpected EOF"), -1;
	if (!headerCheckMagic(lead))
	    return ERRSTR("bad header magic"), -1; // XXX eos
	ssize_t dataSize = headerDataSize(lead);
	if (dataSize < 0)
	    return ERRSTR("bad header size"), -1;
	z->left = 16 + dataSize;
    }
    if (size > z->left) {
	// Can only read z->left.
	ret = reada(&z->fda, buf, z->left);
	if (ret < 0)
	    return ERRNO("read"), -1;
	if (ret != z->left)
	    return ERRSTR("unexpected EOF"), -1;
	buf = (char *) buf + ret, size -= ret, total += ret;
	z->left = 0;
	goto peek;
    }
    // Can serve the whole size.
    ret = reada(&z->fda, buf, size);
    if (ret < 0)
	return ERRNO("read"), -1;
    if (ret != size)
	return ERRSTR("unexpected EOF"), -1;
    z->left -= size;
    return total + size;
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
    if (!z->hasLead) {
	ssize_t ret = peeka(&z->fda, z->lead, 16);
	if (ret < 0)
	    return ERRNO("read"), -1;
	if (ret == 0)
	    return 0;
	if (ret < 16)
	    return ERRSTR("unexpected EOF"), -1;
	if (!headerCheckMagic(z->lead))
	    return ERRSTR("bad header magic"), -1; // XXX eos
	z->hasLead = true;
	// Only skip magic, keep <il,dl>.
	z->fda.cur += 8;
    }
    ssize_t dataSize = headerDataSize(z->lead);
    if (dataSize < 0)
	return ERRSTR("bad header size"), -1;
    size_t blobSize = 8 + dataSize;
    off_t pos = tella(&z->fda) - 8;

    void *buf = generic_opHdrBuf(z, blobSize);
    if (!buf)
	return ERRNO("malloc"), -1;
    ssize_t ret = reada(&z->fda, buf, blobSize);
    if (ret < 0)
	return ERRNO("read"), -1;
    if (ret != blobSize)
	return ERRSTR("unexpected EOF"), -1;

    // Deal with what's next.
    ret = peeka(&z->fda, z->lead, 16);
    if (ret < 0)
	return ERRNO("read"), -1;
    if (ret == 0)
	z->hasLead = false;
    else {
	if (ret < 16)
	    return ERRSTR("unexpected EOF"), -1;
	if (!headerCheckMagic(z->lead))
	    return ERRSTR("bad header magic"), -1; // XXX eos
	z->fda.cur += 8;
    }

    if (posp)
	*posp = pos;
    return blobSize;
}

static ssize_t OP(NextView)(struct zpkglistReader *z, void **bufp, int64_t *posp, const char *err[2])
{
    if (!z->hasLead) {
	ssize_t ret = peeka(&z->fda, z->lead, 16);
	if (ret < 0)
	    return ERRNO("read"), -1;
	if (ret == 0)
	    return 0;
	if (ret < 16)
	    return ERRSTR("unexpected EOF"), -1;
	if (!headerCheckMagic(z->lead))
	    return ERRSTR("bad header magic"), -1; // XXX eos
	z->hasLead = true;
	// Only skip magic, keep <il,dl>.
	z->fda.cur += 8;
    }
    ssize_t dataSize = headerDataSize(z->lead);
    if (dataSize < 0)
	return ERRSTR("bad header size"), -1;

    // Going to return this:
    void *buf;
    size_t blobSize = 8 + dataSize;
    off_t pos = tella(&z->fda) - 8;

    // Gonna peek at the next header, this will be the result of peeka.
    ssize_t ret;

    // Does the blob fit into the fda buffer, along with 16 more bytes
    // of the next blob to peek at?  If it doesn't, resort to malloc.
    if (blobSize + 16 > maxfilla(&z->fda)) {
	buf = generic_opHdrBuf(z, blobSize);
	if (!buf)
	    return ERRNO("malloc"), -1;
	ret = reada(&z->fda, buf, blobSize);
	if (ret < 0)
	    return ERRNO("read"), -1;
	if (ret != blobSize)
	    return ERRSTR("unexpected EOF"), -1;
	ret = peeka(&z->fda, z->lead, 16);
	if (ret < 0)
	    return ERRNO("read"), -1;
    }
    else {
	ret = filla(&z->fda, blobSize + 16);
	if (ret < 0)
	    return ERRNO("read"), -1;
	if (ret < blobSize)
	    return ERRSTR("unexpected EOF"), -1;
	// Take the internal region, as if by reada().
	buf = z->fda.cur;
	z->fda.cur += blobSize;
	// Simulate peeka().
	ret -= blobSize;
	memcpy(z->lead, z->fda.cur, ret);
    }

    // Deal with what's next.
    if (ret == 0)
	z->hasLead = false;
    else {
	if (ret < 16)
	    return ERRSTR("unexpected EOF"), -1;
	if (!headerCheckMagic(z->lead))
	    return ERRSTR("bad header magic"), -1; // XXX eos
	z->fda.cur += 8;
    }

    *bufp = buf;
    if (posp)
	*posp = pos;
    return blobSize;
}

const struct ops OPS = {
    OP(Open),
    OP(Free),
    OP(Read),
    OP(ContentSize),
    OP(Bulk),
    OP(NextMalloc),
    OP(NextView),
};
