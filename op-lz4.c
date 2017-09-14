#include <assert.h>
#include "lz4reader.h"
#include "reader.h"
#include "error.h"

static bool lz4_opOpen(struct zpkglistReader *z, const char *err[2])
{
    struct lz4reader *lz4;
    int ret = lz4reader_fdopen(&lz4, &z->fda, err);
    if (ret < 0)
	return false;
    assert(ret > 0); // starts with the magic
    return z->opState = lz4, true;
}

static void lz4_opClose(struct zpkglistReader *z)
{
    lz4reader_close(z->opState);
}

const struct ops ops_lz4 = {
    lz4_opOpen,
    lz4_opClose,
};
