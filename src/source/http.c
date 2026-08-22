#include "source/http.h"

#include "source/http-parse.h"
#include "source/source-private.h"
#include "util.h"
#include "zget.h"
#include "zget_version.h"

#include <curl/curl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct zget_http_source {
    struct zget_source source;
    CURL *curl;
    char *url;
    char *effective_url;
    char *strong_etag;
    uint64_t requests;
    uint32_t max_requests;
    uint32_t max_redirects;
};

struct response {
    struct zget_http_source *http;
    zget_source_write_cb cb;
    void *userdata;
    uint64_t request_offset;
    uint64_t request_length;
    uint64_t body_length;
    uint64_t range_start;
    uint64_t range_end;
    uint64_t range_total;
    long status;
    bool suffix;
    bool headers_loaded;
    bool header_api_failed;
    bool have_content_range;
    bool bad_content_range;
    bool bad_encoding;
    bool bad_etag;
    bool identity_checked;
    bool identity_valid;
    bool callback_failed;
    bool callback_stopped;
    char *etag;
};

static bool response_load_headers(struct response *r)
{
    struct curl_header *header;
    CURLHcode hc;
    CURLcode cc;
    size_t i, amount;

    if (r->headers_loaded)
        return !r->header_api_failed;
    r->headers_loaded = true;
    cc = curl_easy_getinfo(r->http->curl, CURLINFO_RESPONSE_CODE, &r->status);
    if (cc != CURLE_OK)
        goto api_failure;

    hc = curl_easy_header(r->http->curl, "Content-Range", 0, CURLH_HEADER,
                          -1, &header);
    if (hc == CURLHE_OK) {
        if (header->amount != 1 ||
            !zget_parse_content_range(header->value, &r->range_start,
                                      &r->range_end, &r->range_total))
            r->bad_content_range = true;
        else
            r->have_content_range = true;
    } else if (hc != CURLHE_MISSING && hc != CURLHE_NOHEADERS &&
               hc != CURLHE_NOREQUEST)
        goto header_failure;

    hc = curl_easy_header(r->http->curl, "Content-Encoding", 0, CURLH_HEADER,
                          -1, &header);
    if (hc == CURLHE_OK) {
        amount = header->amount;
        for (i = 0; i < amount; ++i) {
            hc = curl_easy_header(r->http->curl, "Content-Encoding", i,
                                  CURLH_HEADER, -1, &header);
            if (hc != CURLHE_OK)
                goto header_failure;
            if (strcasecmp(header->value, "identity") != 0)
                r->bad_encoding = true;
        }
    } else if (hc != CURLHE_MISSING && hc != CURLHE_NOHEADERS &&
               hc != CURLHE_NOREQUEST)
        goto header_failure;

    hc = curl_easy_header(r->http->curl, "ETag", 0, CURLH_HEADER, -1, &header);
    if (hc == CURLHE_OK) {
        if (header->amount != 1)
            r->bad_etag = true;
        else {
            r->etag = zget_strdup(header->value);
            if (r->etag == NULL) {
                zget_error_set(r->http->source.error, ZGET_ENOMEM,
                               "could not retain response ETag");
                r->header_api_failed = true;
                return false;
            }
        }
    } else if (hc != CURLHE_MISSING && hc != CURLHE_NOHEADERS &&
               hc != CURLHE_NOREQUEST)
        goto header_failure;
    return true;

header_failure:
    if (hc == CURLHE_OUT_OF_MEMORY)
        zget_error_set(r->http->source.error, ZGET_ENOMEM,
                       "libcurl could not retain response headers");
    else
        zget_error_set(r->http->source.error, ZGET_EHTTP,
                       "could not inspect final HTTP response headers");
    r->header_api_failed = true;
    return false;

api_failure:
    zget_error_set(r->http->source.error, ZGET_EHTTP,
                   "could not inspect final HTTP response: %s",
                   curl_easy_strerror(cc));
    r->header_api_failed = true;
    return false;
}

static bool response_range_valid(const struct response *r)
{
    uint64_t wanted, expected_start;

    if (r->status != 206 || !r->have_content_range || r->bad_content_range ||
        r->range_total == 0 ||
        r->range_end >= r->range_total || r->range_start > r->range_end)
        return false;
    wanted = r->request_length < r->range_total ? r->request_length : r->range_total;
    expected_start = r->suffix ? r->range_total - wanted : r->request_offset;
    return r->range_start == expected_start &&
           r->range_end - r->range_start + 1 == wanted &&
           (r->suffix || wanted == r->request_length);
}

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool suffix_status_allows_fallback(long status)
{
    return status == 200 || status == 400 || status == 416 || status == 501;
}

static enum zget_source_action discard_range(void *userdata, const void *data,
                                             size_t size)
{
    (void)userdata;
    (void)data;
    (void)size;
    return ZGET_SOURCE_CONTINUE;
}

static bool response_identity_valid(struct response *r)
{
    if (r->identity_checked)
        return r->identity_valid;
    r->identity_checked = true;

    if (r->http->source.size_known &&
        r->http->source.size != r->range_total) {
        zget_error_set(r->http->source.error, ZGET_ECHANGED,
                       "remote object size changed");
        return false;
    }
    if (r->http->strong_etag != NULL &&
        (r->etag == NULL || starts_with(r->etag, "W/") ||
         strcmp(r->http->strong_etag, r->etag) != 0)) {
        zget_error_set(r->http->source.error, ZGET_ECHANGED,
                       "remote object ETag changed");
        return false;
    }
    r->identity_valid = true;
    return true;
}

static size_t body_cb(char *data, size_t size, size_t nmemb, void *opaque)
{
    struct response *r = opaque;
    size_t n;

    if (nmemb != 0 && size > SIZE_MAX / nmemb)
        return 0;
    n = size * nmemb;
    if (!response_load_headers(r) || !response_range_valid(r) ||
        r->bad_encoding || r->bad_etag || !response_identity_valid(r))
        return 0;
    if (UINT64_MAX - r->body_length < n ||
        r->body_length + n > r->range_end - r->range_start + 1)
        return 0;
    if (n != 0) {
        enum zget_source_action callback_result =
            r->cb(r->userdata, data, n);
        if (callback_result == ZGET_SOURCE_STOP)
            r->callback_stopped = true;
        else if (callback_result != ZGET_SOURCE_CONTINUE)
            r->callback_failed = true;
        if (callback_result != ZGET_SOURCE_CONTINUE)
            return 0;
    }
    r->body_length += n;
    return n;
}

static int perform_range(struct zget_http_source *http, uint64_t offset,
                         uint64_t length, bool suffix,
                         zget_source_write_cb cb, void *userdata,
                         bool *suffix_fallback)
{
    struct response r = {0};
    struct curl_slist *headers = NULL;
    char range[64];
    char *if_match = NULL;
    struct curl_slist *next;
    char *effective = NULL;
    const char *request_url;
    CURLcode cc;
    uint64_t expected;

    if (suffix_fallback != NULL)
        *suffix_fallback = false;
    if (length == 0 || cb == NULL) {
        zget_error_set(http->source.error, ZGET_EINVAL,
                       "invalid zero-length range request");
        return ZGET_EINVAL;
    }
    if (http->max_requests != 0 && http->requests >= http->max_requests) {
        zget_error_set(http->source.error, ZGET_ELIMIT,
                       "HTTP request limit exceeded");
        return ZGET_ELIMIT;
    }
    if (suffix)
        (void)snprintf(range, sizeof(range), "-%" PRIu64, length);
    else {
        uint64_t end;
        if (!zget_u64_add(offset, length - 1, &end)) {
            zget_error_set(http->source.error, ZGET_EINVAL,
                           "range offset overflows uint64_t");
            return ZGET_EINVAL;
        }
        (void)snprintf(range, sizeof(range), "%" PRIu64 "-%" PRIu64,
                       offset, end);
    }

    r.http = http;
    r.cb = cb;
    r.userdata = userdata;
    r.request_offset = offset;
    r.request_length = length;
    r.suffix = suffix;
    request_url = http->effective_url != NULL ? http->effective_url : http->url;

    headers = curl_slist_append(NULL, "Accept-Encoding: identity");
    if (headers == NULL) {
        zget_error_set(http->source.error, ZGET_ENOMEM,
                       "could not allocate HTTP headers");
        goto done;
    }
    if (http->strong_etag != NULL) {
        size_t etag_length = strlen(http->strong_etag);
        if (etag_length > SIZE_MAX - sizeof("If-Match: ")) {
            zget_error_set(http->source.error, ZGET_ENOMEM,
                           "ETag is too large");
            goto done;
        }
        if_match = malloc(sizeof("If-Match: ") + etag_length);
        if (if_match == NULL) {
            zget_error_set(http->source.error, ZGET_ENOMEM,
                           "could not allocate If-Match header");
            goto done;
        }
        (void)snprintf(if_match, sizeof("If-Match: ") + etag_length,
                       "If-Match: %s", http->strong_etag);
        next = curl_slist_append(headers, if_match);
        if (next == NULL) {
            zget_error_set(http->source.error, ZGET_ENOMEM,
                           "could not allocate HTTP headers");
            goto done;
        }
        headers = next;
    }
#define SETOPT(opt, value) do { cc = curl_easy_setopt(http->curl, opt, value); \
    if (cc != CURLE_OK) goto curl_failure; } while (0)
    SETOPT(CURLOPT_URL, request_url);
    SETOPT(CURLOPT_RANGE, range);
    SETOPT(CURLOPT_HTTPHEADER, headers);
    SETOPT(CURLOPT_WRITEFUNCTION, body_cb);
    SETOPT(CURLOPT_WRITEDATA, &r);
    ++http->requests;

    fprintf(stderr, "zget-debug: range perform begin suffix=%d range=%s\n",
            suffix ? 1 : 0, range);
    fflush(stderr);
    cc = curl_easy_perform(http->curl);
    fprintf(stderr, "zget-debug: range perform end curl=%d\n", (int)cc);
    fflush(stderr);

    if (!response_load_headers(&r))
        goto done;
    fprintf(stderr, "zget-debug: range status=%ld\n", r.status);
    fflush(stderr);
    if (r.identity_checked && !r.identity_valid)
        goto done;
    if (r.callback_failed) {
        if (http->source.error->code == ZGET_OK)
            zget_error_set(http->source.error, ZGET_EIO,
                           "range consumer rejected output");
        goto done;
    }
    if (r.callback_stopped) {
        http->source.error->code = ZGET_OK;
        http->source.error->message[0] = '\0';
        goto done;
    }
    if (r.status == 200) {
        fprintf(stderr, "zget-debug: range ignored with HTTP 200\n");
        fflush(stderr);
        if (suffix && suffix_fallback != NULL)
            *suffix_fallback = true;
        zget_error_set(http->source.error, ZGET_ERANGE,
                       "server ignored the required byte range");
        goto done;
    }
    if (r.status == 412) {
        zget_error_set(http->source.error, ZGET_ECHANGED,
                       "remote object changed");
        goto done;
    }
    if (r.status == 0 && cc != CURLE_OK) {
        zget_error_set(http->source.error, ZGET_EHTTP,
                       "HTTP transfer failed: %s", curl_easy_strerror(cc));
        goto done;
    }
    if (r.status != 206) {
        if (suffix && suffix_fallback != NULL &&
            suffix_status_allows_fallback(r.status))
            *suffix_fallback = true;
        zget_error_set(http->source.error, ZGET_EHTTP,
                       "server returned HTTP %ld for byte-range request",
                       r.status);
        goto done;
    }
    if (!response_range_valid(&r)) {
        zget_error_set(http->source.error, ZGET_EHTTP,
                       "invalid Content-Range for requested byte range");
        goto done;
    }
    if (r.bad_encoding) {
        zget_error_set(http->source.error, ZGET_EHTTP,
                       "server returned a non-identity Content-Encoding");
        goto done;
    }
    if (r.bad_etag) {
        zget_error_set(http->source.error, ZGET_EHTTP,
                       "server returned multiple ETag headers");
        goto done;
    }
    if (!response_identity_valid(&r))
        goto done;
    expected = r.range_end - r.range_start + 1;
    if (r.body_length != expected) {
        zget_error_set(http->source.error, ZGET_EHTTP,
                       "truncated HTTP range body");
        goto done;
    }
    if (cc != CURLE_OK) {
        zget_error_set(http->source.error, ZGET_EHTTP,
                       "HTTP transfer failed: %s", curl_easy_strerror(cc));
        goto done;
    }
    if (!http->source.size_known) {
        http->source.size = r.range_total;
        http->source.size_known = true;
        if (r.etag != NULL && !starts_with(r.etag, "W/")) {
            http->strong_etag = r.etag;
            r.etag = NULL;
        }
    }
    if (curl_easy_getinfo(http->curl, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK &&
        effective != NULL && http->effective_url == NULL) {
        http->effective_url = zget_strdup(effective);
        if (http->effective_url == NULL) {
            zget_error_set(http->source.error, ZGET_ENOMEM,
                           "could not retain effective URL");
            goto done;
        }
    }
    http->source.error->code = ZGET_OK;
    http->source.error->message[0] = '\0';

done:
    free(r.etag);
    free(if_match);
    curl_slist_free_all(headers);
    (void)curl_easy_setopt(http->curl, CURLOPT_HTTPHEADER, NULL);
    return http->source.error->code;

curl_failure:
    zget_error_set(http->source.error, ZGET_EHTTP,
                   "could not configure HTTP transfer: %s",
                   curl_easy_strerror(cc));
    goto done;
#undef SETOPT
}

static int http_read_range(struct zget_source *source, uint64_t offset,
                           uint64_t length, zget_source_write_cb write_cb,
                           void *userdata)
{
    return perform_range((struct zget_http_source *)source, offset, length,
                         false, write_cb, userdata, NULL);
}

static int http_read_suffix(struct zget_source *source, uint64_t length,
                            zget_source_write_cb write_cb, void *userdata)
{
    struct zget_http_source *http = (struct zget_http_source *)source;
    uint64_t explicit_length;
    bool fallback;
    int rc;

    rc = perform_range(http, 0, length, true, write_cb, userdata, &fallback);
    if (rc == ZGET_OK || !fallback)
        return rc;

    http->source.error->code = ZGET_OK;
    http->source.error->message[0] = '\0';
    rc = perform_range(http, 0, 1, false, discard_range, NULL, NULL);
    if (rc != ZGET_OK)
        return rc;

    explicit_length = length < http->source.size ? length : http->source.size;
    return perform_range(http, http->source.size - explicit_length,
                         explicit_length, false, write_cb, userdata, NULL);
}

static void http_close(struct zget_source *source)
{
    struct zget_http_source *http = (struct zget_http_source *)source;

    if (http->curl != NULL)
        curl_easy_cleanup(http->curl);
    free(http->url);
    free(http->effective_url);
    free(http->strong_etag);
    free(http);
}

static const struct zget_source_ops http_ops = {
    .read_range = http_read_range,
    .read_suffix = http_read_suffix,
    .close = http_close
};

int zget_http_global_init(void)
{
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ?
           ZGET_OK : ZGET_EHTTP;
}

void zget_http_global_cleanup(void)
{
    curl_global_cleanup();
}

int zget_http_source_open(const char *url,
                          const struct zget_http_options *options,
                          struct zget_error_state *error,
                          struct zget_source **out_source)
{
    struct zget_http_source *http_source = NULL;
    CURLcode cc;
    CURLUcode uc;
    CURLU *parsed_url;
    char *scheme = NULL;
    bool https, http;

    if (out_source == NULL)
        return ZGET_EINVAL;
    *out_source = NULL;
    if (url == NULL || url[0] == '\0' || options == NULL || error == NULL)
        return ZGET_EINVAL;
    error->code = ZGET_OK;
    error->message[0] = '\0';

    parsed_url = curl_url();
    if (parsed_url == NULL) {
        zget_error_set(error, ZGET_ENOMEM, "could not allocate URL parser");
        return ZGET_ENOMEM;
    }
    uc = curl_url_set(parsed_url, CURLUPART_URL, url, 0);
    if (uc == CURLUE_OK)
        uc = curl_url_get(parsed_url, CURLUPART_SCHEME, &scheme, 0);
    if (uc != CURLUE_OK) {
        int rc = uc == CURLUE_OUT_OF_MEMORY ? ZGET_ENOMEM : ZGET_EINVAL;
        zget_error_set(error, rc, "invalid URL: %s", curl_url_strerror(uc));
        curl_free(scheme);
        curl_url_cleanup(parsed_url);
        return rc;
    }
    https = strcasecmp(scheme, "https") == 0;
    http = strcasecmp(scheme, "http") == 0;
    curl_free(scheme);
    curl_url_cleanup(parsed_url);
    if (!https && !http) {
        zget_error_set(error, ZGET_EINVAL, "URL must use HTTP or HTTPS");
        return ZGET_EINVAL;
    }

    http_source = calloc(1, sizeof(*http_source));
    if (http_source == NULL) {
        zget_error_set(error, ZGET_ENOMEM, "could not allocate HTTP source");
        return ZGET_ENOMEM;
    }
    zget_source_init(&http_source->source, &http_ops, error);
    http_source->max_requests = options->max_requests;
    http_source->max_redirects = options->max_redirects;
    http_source->url = zget_strdup(url);
    if (http_source->url == NULL) {
        zget_error_set(error, ZGET_ENOMEM, "could not retain source URL");
        goto fail;
    }
    http_source->curl = curl_easy_init();
    if (http_source->curl == NULL) {
        zget_error_set(error, ZGET_ENOMEM, "could not create libcurl handle");
        goto fail;
    }
#define SETOPT(opt, value) do { \
    cc = curl_easy_setopt(http_source->curl, opt, value); \
    if (cc != CURLE_OK) goto fail; } while (0)
    SETOPT(CURLOPT_FOLLOWLOCATION, 1L);
    SETOPT(CURLOPT_MAXREDIRS, (long)http_source->max_redirects);
    SETOPT(CURLOPT_PROTOCOLS_STR, "http,https");
    SETOPT(CURLOPT_REDIR_PROTOCOLS_STR, https ? "https" : "http,https");
    SETOPT(CURLOPT_FAILONERROR, 0L);
    SETOPT(CURLOPT_NOSIGNAL, 1L);
    SETOPT(CURLOPT_USERAGENT, "zget/" ZGET_VERSION_STRING);
    *out_source = &http_source->source;
    return ZGET_OK;
fail:
    if (error->code == ZGET_OK)
        zget_error_set(error, ZGET_EHTTP, "could not configure libcurl: %s",
                       curl_easy_strerror(cc));
    http_close(&http_source->source);
    return error->code;
#undef SETOPT
}
