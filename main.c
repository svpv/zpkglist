#include <assert.h>
#include <unistd.h>
#include "zpkglist.h"

int main()
{
    assert(!isatty(0));
    assert(!isatty(1));
    return zpkglistCompress(0, 1, NULL, NULL) ? 0 : 1;
}
