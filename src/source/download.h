#ifndef ZGET_SOURCE_DOWNLOAD_H
#define ZGET_SOURCE_DOWNLOAD_H

#include "error.h"
#include "source/http.h"

/*
 * Download one complete identity representation into an anonymous temporary
 * file. On success the caller owns *out_fd and must close or transfer it.
 */
int zget_http_download_to_temp(const char *url,
                               const struct zget_http_options *options,
                               struct zget_error_state *error, int *out_fd);

#endif
