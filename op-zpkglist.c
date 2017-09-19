#include <stdlib.h>
#include "reader.h"
#include "error.h"

struct readState {
    char *cur;
    char *end;
};

ssize_t generic_opRead(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    struct readState *s = z->readState;
    if (!s) {
	s = z->readState = malloc(sizeof *s);
	if (!s)
	    return ERRNO("malloc"), -1;
	*s = (struct readState) { NULL, NULL };
    }

    size_t total = 0;
    do {
	size_t left = s->end - s->cur;
	if (left == 0) {
	    ssize_t ret = z->ops->opBulk(z, (void **) &s->cur, err);
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

#include "zreader.h"
#include "reada.h"

#define OP(op) CAT2(zpkglist_op, op)
#define OPS ops_zpkglist

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

static ssize_t OP(Bulk)(struct zpkglistReader *z, void **bufp, const char *err[2])
{
    return zreader_getFrame(z->opState, bufp, err);
}

const struct ops OPS = {
    OP(Open),
    OP(Reopen),
    OP(Free),
    generic_opRead,
    OP(ContentSize),
    OP(Bulk),
};
