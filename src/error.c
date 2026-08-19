#include "error.h"

#include <stdarg.h>
#include <stdio.h>

void zget_error_set(struct zget_error_state *error, int code,
                    const char *fmt, ...)
{
    va_list ap;

    if (error == NULL)
        return;
    error->code = code;
    va_start(ap, fmt);
    (void)vsnprintf(error->message, sizeof(error->message), fmt, ap);
    va_end(ap);
}
