#include <zget.h>
#include <stdio.h>
/* Prove an installed consumer can link and call the public library API. */
int main(void)
{
    puts(zget_version());
    return 0;
}
