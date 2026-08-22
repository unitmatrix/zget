#define _POSIX_C_SOURCE 200809L

#include "source/download.h"

#include "zget.h"
#include "zget_version.h"

#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct download_state {
    int fd;
    int saved_errno;
};

static size_t download_write_cb(char *data, size_t size, size_t nmemb,
                                void *opaque)
{
    struct download_state *state = opaque;
    size_t total, written = 0;

    if (nmemb != 0 && size > SIZE_MAX / nmemb)
        return 0;
    total = size * nmemb;
    while (written < total) {
        ssize_t n;
        do {
            n = write(state->fd, data + written, total - written);
        } while (n < 0 && errno == EINTR);
        if (n <= 0) {
            state->saved_errno = n < 0 ? errno : EIO;
            return 0;
        }
        written += (size_t)n;
    }
    return total;
}

int zget_http_download_to_temp(const char *url,
                               const struct zget_http_options *options,
                               struct zget_error_state *error, int *out_fd)
{
    char path[] = "/tmp/zget-XXXXXX";
    struct download_state state;
    struct curl_slist *headers = NULL;
    CURL *curl = NULL;
    CURLcode cc;
    long status = 0;
    int fd = -1, rc = ZGET_EHTTP;

    if (out_fd != NULL)
        *out_fd = -1;
    if (url == NULL || options == NULL || error == NULL || out_fd == NULL)
        return ZGET_EINVAL;

    fd = mkstemp(path);
    if (fd < 0) {
        zget_error_set(error, ZGET_EIO, "create temporary file: %s",
                       strerror(errno));
        return ZGET_EIO;
    }
    /* The fallback is an implementation detail; never expose a named file. */
    if (unlink(path) != 0) {
        int saved_errno = errno;
        close(fd);
        zget_error_set(error, ZGET_EIO, "unlink temporary file: %s",
                       strerror(saved_errno));
        return ZGET_EIO;
    }
    curl = curl_easy_init();
    if (curl == NULL) {
        zget_error_set(error, ZGET_ENOMEM, "could not create HTTP handle");
        rc = ZGET_ENOMEM;
        goto done;
    }
    headers = curl_slist_append(NULL, "Accept-Encoding: identity");
    if (headers == NULL) {
        zget_error_set(error, ZGET_ENOMEM, "could not allocate HTTP headers");
        rc = ZGET_ENOMEM;
        goto done;
    }
    state.fd = fd;
    state.saved_errno = 0;
#define SETOPT(opt, value) do { cc = curl_easy_setopt(curl, opt, value); \
    if (cc != CURLE_OK) goto curl_failure; } while (0)
    SETOPT(CURLOPT_URL, url);
    SETOPT(CURLOPT_HTTPHEADER, headers);
    SETOPT(CURLOPT_FOLLOWLOCATION, 1L);
    SETOPT(CURLOPT_MAXREDIRS, (long)options->max_redirects);
    SETOPT(CURLOPT_PROTOCOLS_STR, "http,https");
    SETOPT(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    SETOPT(CURLOPT_USERAGENT, "zget/" ZGET_VERSION_STRING);
    SETOPT(CURLOPT_WRITEFUNCTION, download_write_cb);
    SETOPT(CURLOPT_WRITEDATA, &state);
    cc = curl_easy_perform(curl);
    if (state.saved_errno != 0) {
        zget_error_set(error, ZGET_EIO, "write temporary file: %s",
                       strerror(state.saved_errno));
        rc = ZGET_EIO;
        goto done;
    }
    if (cc != CURLE_OK) {
        zget_error_set(error, ZGET_EHTTP, "HTTP transfer failed: %s",
                       curl_easy_strerror(cc));
        goto done;
    }
    cc = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (cc != CURLE_OK)
        goto curl_failure;
    if (status < 200 || status >= 300) {
        zget_error_set(error, ZGET_EHTTP,
                       "server returned HTTP %ld for complete download", status);
        goto done;
    }
    *out_fd = fd;
    fd = -1;
    rc = ZGET_OK;
    goto done;

curl_failure:
    zget_error_set(error, ZGET_EHTTP, "could not configure HTTP transfer: %s",
                   curl_easy_strerror(cc));
done:
    curl_slist_free_all(headers);
    if (curl != NULL)
        curl_easy_cleanup(curl);
    if (fd >= 0)
        close(fd);
    return rc;
#undef SETOPT
}
