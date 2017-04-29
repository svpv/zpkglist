#include <stdio.h>
#include <assert.h>
#include INC

int main()
{
    size_t ret = fwrite(NAME, 1, sizeof NAME, stdout);
    assert(ret == sizeof NAME);
    assert(fflush(stdout) == 0);
    return 0;
}
