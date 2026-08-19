#ifndef ZGET_FORMAT_ZIP_H
#define ZGET_FORMAT_ZIP_H

#include "format/format.h"

int zget_zip_format_open(struct zget_source *source,
                         const struct zget_format_options *options,
                         struct zget_error_state *error,
                         struct zget_format **out_format);

#endif
