#ifndef ZGET_FORMAT_H
#define ZGET_FORMAT_H

#include "error.h"
#include "source/source.h"
#include "zget.h"

struct zget_format;

int zget_format_open(struct zget_source *source,
                     struct zget_error_state *error,
                     struct zget_format **out_format);
int zget_format_extract_member(struct zget_format *format, const char *member,
                               zget_write_cb write_cb, void *userdata);
int zget_format_list(struct zget_format *format, zget_list_cb list_cb,
                     void *userdata);
void zget_format_close(struct zget_format *format);

#endif
