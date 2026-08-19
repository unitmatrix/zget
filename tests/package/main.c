#include <zget.h>
#include <stdio.h>
/* Prove an installed consumer can link and call the public library API. */
int main(void)
{
    if (zget_global_init() != ZGET_OK)
        return 1;
    puts(zget_version());
    zget_global_cleanup();
    return 0;
}
