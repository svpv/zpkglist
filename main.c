#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include "zpkglist.h"

int main()
{
    assert(!isatty(0));
    assert(!isatty(1));
    const char *err[2];
    if (zpkglistCompress(0, 1, err, NULL, NULL))
	return 0;
    fprintf(stderr, "%s: %s\n", err[0], err[1]);
    fprintf(stderr, "zpkglist: compression failed\n");
    return 1;
}
