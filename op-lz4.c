#include <assert.h>
#include "lz4reader.h"
#include "reader.h"
#include "error.h"
#include "magic4.h"

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

static ssize_t lz4_opRead(struct zpkglistReader *z, void *buf, size_t size, const char *err[2])
{
    ssize_t total = 0;
    do {
	ssize_t n = lz4reader_read(z->opState, buf, size, err);
	if (n < 0)
	    return -1;
	// May need to start the next frame.
	if (n == 0) {
	    unsigned w;
	    n = peeka(&z->fda, &w, 4);
	    if (n < 0)
		return ERRNO("read"), -1;
	    if (n == 0) // true EOF
		return total;
	    if (n < 4 || w != MAGIC4_W_LZ4)
		return ERRSTR("trailing garbage"), -1;
	    int ret = lz4reader_nextFrame(z->opState, err);
	    if (ret < 0)
		return -1;
	    assert(ret > 0);
	    continue;
	}
	buf = (char *) buf + n;
	size -= n;
	total += n;
    } while (size);
    return total;
}

const struct ops ops_lz4 = {
    lz4_opOpen,
    lz4_opClose,
    lz4_opRead,
};
