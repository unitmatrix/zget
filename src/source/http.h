#ifndef ZGET_SOURCE_HTTP_H
#define ZGET_SOURCE_HTTP_H

#include "error.h"
#include "source/source.h"

#include <stdint.h>

struct zget_http_options {
    uint32_t max_redirects;
};

int zget_http_global_init(void);
void zget_http_global_cleanup(void);
int zget_http_source_open(const char *url,
                          const struct zget_http_options *options,
                          struct zget_error_state *error,
                          struct zget_source **out_source);

#endif
