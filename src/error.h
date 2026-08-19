#ifndef ZGET_ERROR_H
#define ZGET_ERROR_H

/*
 * Every internal layer reports through the same compact error state without
 * depending on zget_ctx. This keeps sources and format engines independently
 * testable while preserving one detailed diagnostic for public callers.
 */
struct zget_error_state {
    int code;
    char message[256];
};

void zget_error_set(struct zget_error_state *error, int code,
                    const char *fmt, ...);

#endif
