#ifndef ZGET_SOURCE_FILE_H
#define ZGET_SOURCE_FILE_H

#include "error.h"
#include "source/source.h"

/*
 * Open a seekable local file as a source. The returned source owns its file
 * descriptor and releases it from zget_source_close().
 */
int zget_file_source_open(const char *path, struct zget_error_state *error,
                          struct zget_source **out_source);

#endif
