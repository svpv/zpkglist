#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include "zpkglist.h"

#define PROG "zpkglist"

int main()
{
    assert(!isatty(0));
    assert(!isatty(1));
    const char *err[2];
    int ret = zpkglistCompress(0, 1, err, NULL, NULL);
    if (ret == 0)
	fprintf(stderr, PROG ": warning: empty input (valid output still written)\n");
    if (ret >= 0)
	return 0;
    fprintf(stderr, "%s: %s\n", err[0], err[1]);
    fprintf(stderr, PROG ": compression failed\n");
    return 1;
}
