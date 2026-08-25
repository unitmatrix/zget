#ifndef ZGET_FORMAT_ZIP_H
#define ZGET_FORMAT_ZIP_H

#include "format/format.h"

int zget_zip_format_open(struct zget_source *source,
                         struct zget_error_state *error,
                         struct zget_format **out_format);
int zget_zip_format_extract_member(struct zget_format *format,
                                   const char *member,
                                   zget_write_cb write_cb, void *userdata);
int zget_zip_format_list(struct zget_format *format, zget_list_cb list_cb,
                         void *userdata);
void zget_zip_format_close(struct zget_format *format);

#endif
