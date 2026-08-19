#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * Per-transfer state is intentionally separate from zget_ctx. Only headers
 * from libcurl's final request are loaded, so redirects and informational
 * responses cannot contribute range metadata or an archive validator.
 */
struct response {
    struct zget_ctx *ctx;
    zget_write_cb cb;
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

/* Content-Range grammar parsing lives in util.c so it has no libcurl
 * dependency and can be exercised by a standalone fuzz target. */

/*
 * Load only the final request's ordinary response headers. curl_easy_header()
 * owns and normalizes the header storage; zget retains only the ETag that must
 * survive later requests. This function is safe from a body callback, which is
 * how validation remains ahead of every byte exposed to a ZIP/output consumer.
 */
static bool response_load_headers(struct response *r)
{
    struct curl_header *header;
    CURLHcode hc;
    CURLcode cc;
    size_t i, amount;

    if (r->headers_loaded)
        return !r->header_api_failed;
    r->headers_loaded = true;
    cc = curl_easy_getinfo(r->ctx->curl, CURLINFO_RESPONSE_CODE, &r->status);
    if (cc != CURLE_OK)
        goto api_failure;

    hc = curl_easy_header(r->ctx->curl, "Content-Range", 0, CURLH_HEADER,
                          -1, &header);
    if (hc == CURLHE_OK) {
        /* Content-Range is a singleton; duplicates are inherently ambiguous. */
        if (header->amount != 1 ||
            !zget_parse_content_range(header->value, &r->range_start,
                                      &r->range_end, &r->range_total))
            r->bad_content_range = true;
        else
            r->have_content_range = true;
    } else if (hc != CURLHE_MISSING && hc != CURLHE_NOHEADERS &&
               hc != CURLHE_NOREQUEST)
        goto header_failure;

    hc = curl_easy_header(r->ctx->curl, "Content-Encoding", 0, CURLH_HEADER,
                          -1, &header);
    if (hc == CURLHE_OK) {
        amount = header->amount;
        for (i = 0; i < amount; ++i) {
            hc = curl_easy_header(r->ctx->curl, "Content-Encoding", i,
                                  CURLH_HEADER, -1, &header);
            if (hc != CURLHE_OK)
                goto header_failure;
            /* Every listed coding must preserve the identity representation. */
            if (strcasecmp(header->value, "identity") != 0)
                r->bad_encoding = true;
        }
    } else if (hc != CURLHE_MISSING && hc != CURLHE_NOHEADERS &&
               hc != CURLHE_NOREQUEST)
        goto header_failure;

    hc = curl_easy_header(r->ctx->curl, "ETag", 0, CURLH_HEADER, -1, &header);
    if (hc == CURLHE_OK) {
        /* ETag is also a singleton; choosing among duplicates is unsafe. */
        if (header->amount != 1)
            r->bad_etag = true;
        else {
            r->etag = zget_strdup(header->value);
            if (r->etag == NULL) {
                zget_set_error(r->ctx, ZGET_ENOMEM,
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
        zget_set_error(r->ctx, ZGET_ENOMEM,
                       "libcurl could not retain response headers");
    else
        zget_set_error(r->ctx, ZGET_EHTTP,
                       "could not inspect final HTTP response headers");
    r->header_api_failed = true;
    return false;

api_failure:
    zget_set_error(r->ctx, ZGET_EHTTP,
                   "could not inspect final HTTP response: %s",
                   curl_easy_strerror(cc));
    r->header_api_failed = true;
    return false;
}

static bool response_range_valid(const struct response *r)
{
    uint64_t wanted, expected_start;

    /*
     * ZIP offsets are meaningful only if the server returned the precise bytes
     * requested. A 206 alone is insufficient: verify the unit, inclusive
     * endpoints, total object size, and suffix semantics before accepting data.
     * For a suffix longer than the object, RFC range behavior returns the whole
     * object; exact ranges must always retain their requested length.
     */
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

/*
 * Prove that this response belongs to the same remote object as the archive
 * tail before any of its body reaches a parser or the caller. If this check
 * were deferred until curl_easy_perform() returned, a changed payload could
 * already have been written to stdout, and the Central Directory parser's
 * intentional early stop could skip the check altogether.
 *
 * The first tail response establishes identity only after its complete body is
 * accepted. Later responses can be checked immediately because archive_size
 * and, when available, strong_etag are immutable context state.
 */
static bool response_identity_valid(struct response *r)
{
    if (r->identity_checked)
        return r->identity_valid;
    r->identity_checked = true;

    if (r->ctx->archive_size != 0 &&
        r->ctx->archive_size != r->range_total) {
        zget_set_error(r->ctx, ZGET_ECHANGED, "remote object size changed");
        return false;
    }
    if (r->ctx->strong_etag != NULL &&
        (r->etag == NULL || starts_with(r->etag, "W/") ||
         strcmp(r->ctx->strong_etag, r->etag) != 0)) {
        zget_set_error(r->ctx, ZGET_ECHANGED, "remote object ETag changed");
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
    /*
     * Validate final-response metadata before forwarding even the first byte.
     * A server that ignores Range is therefore stopped immediately, and a
     * transformed representation can never reach ZIP parsing or caller output.
     */
    if (!response_load_headers(r) || !response_range_valid(r) ||
        r->bad_encoding || r->bad_etag || !response_identity_valid(r))
        return 0;
    if (UINT64_MAX - r->body_length < n ||
        r->body_length + n > r->range_end - r->range_start + 1)
        return 0;
    if (n != 0) {
        int callback_result = r->cb(r->userdata, data, n);
        /* -1 is reserved internally for a successful early CD match. */
        if (callback_result == -1)
            r->callback_stopped = true;
        else if (callback_result != 0)
            r->callback_failed = true;
        if (callback_result != 0)
            return 0;
    }
    r->body_length += n;
    return n;
}

/*
 * Execute exactly one HTTP byte-range request and validate it as an atomic
 * operation. No caller sees bytes until final-response headers prove that they
 * correspond to the requested interval and identity representation.
 */
static int perform_range(struct zget_ctx *ctx, uint64_t offset, uint64_t length,
                         bool suffix, zget_write_cb cb, void *userdata)
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

    if (length == 0 || cb == NULL) {
        zget_set_error(ctx, ZGET_EINVAL, "invalid zero-length range request");
        return ZGET_EINVAL;
    }
    if (ctx->options.max_http_requests != 0 &&
        ctx->http_requests >= ctx->options.max_http_requests) {
        zget_set_error(ctx, ZGET_ELIMIT, "HTTP request limit exceeded");
        return ZGET_ELIMIT;
    }
    if (suffix)
        (void)snprintf(range, sizeof(range), "-%" PRIu64, length);
    else {
        uint64_t end;
        if (!zget_u64_add(offset, length - 1, &end)) {
            zget_set_error(ctx, ZGET_EZIP, "range offset overflows uint64_t");
            return ZGET_EZIP;
        }
        (void)snprintf(range, sizeof(range), "%" PRIu64 "-%" PRIu64,
                       offset, end);
    }

    r.ctx = ctx;
    r.cb = cb;
    r.userdata = userdata;
    r.request_offset = offset;
    r.request_length = length;
    r.suffix = suffix;
    request_url = ctx->effective_url != NULL ? ctx->effective_url : ctx->url;

    /*
     * Do not use CURLOPT_ACCEPT_ENCODING: enabling transparent decoding would
     * sever the relationship between HTTP byte positions and ZIP offsets.
     */
    headers = curl_slist_append(NULL, "Accept-Encoding: identity");
    if (headers == NULL) {
        zget_set_error(ctx, ZGET_ENOMEM, "could not allocate HTTP headers");
        goto done;
    }
    if (ctx->strong_etag != NULL) {
        size_t etag_length = strlen(ctx->strong_etag);
        if (etag_length > SIZE_MAX - sizeof("If-Match: ")) {
            zget_set_error(ctx, ZGET_ENOMEM, "ETag is too large");
            goto done;
        }
        if_match = malloc(sizeof("If-Match: ") + etag_length);
        if (if_match == NULL) {
            zget_set_error(ctx, ZGET_ENOMEM, "could not allocate If-Match header");
            goto done;
        }
        (void)snprintf(if_match, sizeof("If-Match: ") + etag_length,
                       "If-Match: %s", ctx->strong_etag);
        /* A strong validator makes every later metadata/payload request conditional. */
        next = curl_slist_append(headers, if_match);
        if (next == NULL) {
            zget_set_error(ctx, ZGET_ENOMEM, "could not allocate HTTP headers");
            goto done;
        }
        headers = next;
    }
#define SETOPT(opt, value) do { cc = curl_easy_setopt(ctx->curl, opt, value); \
    if (cc != CURLE_OK) goto curl_failure; } while (0)
    SETOPT(CURLOPT_URL, request_url);
    SETOPT(CURLOPT_RANGE, range);
    SETOPT(CURLOPT_HTTPHEADER, headers);
    SETOPT(CURLOPT_WRITEFUNCTION, body_cb);
    SETOPT(CURLOPT_WRITEDATA, &r);
    ++ctx->http_requests;
    cc = curl_easy_perform(ctx->curl);

    /* Zero-length/error responses may never invoke body_cb; inspect them here. */
    if (!response_load_headers(&r))
        goto done;
    /* Preserve the precise identity error set before body_cb stopped curl. */
    if (r.identity_checked && !r.identity_valid)
        goto done;
    if (r.callback_failed) {
        if (ctx->error == ZGET_OK)
            zget_set_error(ctx, ZGET_EIO, "range consumer rejected output");
        goto done;
    }
    if (r.callback_stopped) {
        /*
         * The CD parser found the first exact match. libcurl reports its short
         * write as an error, but cancellation is the desired performance path.
         */
        ctx->error = ZGET_OK;
        ctx->message[0] = '\0';
        goto done;
    }
    if (r.status == 200) {
        zget_set_error(ctx, ZGET_ERANGE,
                       "server ignored the required byte range");
        goto done;
    }
    if (r.status == 412) {
        zget_set_error(ctx, ZGET_ECHANGED, "remote object changed");
        goto done;
    }
    if (r.status == 0 && cc != CURLE_OK) {
        zget_set_error(ctx, ZGET_EHTTP, "HTTP transfer failed: %s",
                       curl_easy_strerror(cc));
        goto done;
    }
    if (!response_range_valid(&r)) {
        zget_set_error(ctx, ZGET_EHTTP, "invalid HTTP status or Content-Range");
        goto done;
    }
    if (r.bad_encoding) {
        zget_set_error(ctx, ZGET_ERANGE,
                       "server returned a non-identity Content-Encoding");
        goto done;
    }
    if (r.bad_etag) {
        zget_set_error(ctx, ZGET_EHTTP, "server returned multiple ETag headers");
        goto done;
    }
    /* Also cover a response whose body was empty and never invoked body_cb. */
    if (!response_identity_valid(&r))
        goto done;
    expected = r.range_end - r.range_start + 1;
    /* Content-Range can be correct while the connection body is truncated. */
    if (r.body_length != expected) {
        zget_set_error(ctx, ZGET_EHTTP, "truncated HTTP range body");
        goto done;
    }
    if (cc != CURLE_OK) {
        zget_set_error(ctx, ZGET_EHTTP, "HTTP transfer failed: %s",
                       curl_easy_strerror(cc));
        goto done;
    }
    if (ctx->archive_size == 0) {
        /* Weak ETags cannot safely back If-Match and are deliberately ignored. */
        ctx->archive_size = r.range_total;
        if (r.etag != NULL && !starts_with(r.etag, "W/")) {
            ctx->strong_etag = r.etag;
            r.etag = NULL;
        }
    }
    if (curl_easy_getinfo(ctx->curl, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK &&
        effective != NULL && ctx->effective_url == NULL) {
        /* Reuse the resolved URL so later ranges do not replay an expiring chain. */
        ctx->effective_url = zget_strdup(effective);
        if (ctx->effective_url == NULL) {
            zget_set_error(ctx, ZGET_ENOMEM, "could not retain effective URL");
            goto done;
        }
    }
    ctx->error = ZGET_OK;
    ctx->message[0] = '\0';

done:
    /* curl_slist does not copy ownership to the easy handle. Detach before free. */
    free(r.etag);
    free(if_match);
    curl_slist_free_all(headers);
    (void)curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, NULL);
    return ctx->error;

curl_failure:
    zget_set_error(ctx, ZGET_EHTTP, "could not configure HTTP transfer: %s",
                   curl_easy_strerror(cc));
    goto done;
#undef SETOPT
}

int zget_http_init(struct zget_ctx *ctx)
{
    CURLcode cc;
    CURLUcode uc;
    CURLU *parsed_url;
    char *scheme = NULL;
    bool https, http;

    parsed_url = curl_url();
    if (parsed_url == NULL) {
        zget_set_error(ctx, ZGET_ENOMEM, "could not allocate URL parser");
        return ZGET_ENOMEM;
    }
    uc = curl_url_set(parsed_url, CURLUPART_URL, ctx->url, 0);
    if (uc == CURLUE_OK)
        uc = curl_url_get(parsed_url, CURLUPART_SCHEME, &scheme, 0);
    if (uc != CURLUE_OK) {
        zget_set_error(ctx, uc == CURLUE_OUT_OF_MEMORY ? ZGET_ENOMEM : ZGET_EINVAL,
                       "invalid URL: %s", curl_url_strerror(uc));
        curl_url_cleanup(parsed_url);
        return uc == CURLUE_OUT_OF_MEMORY ? ZGET_ENOMEM : ZGET_EINVAL;
    }
    https = strcasecmp(scheme, "https") == 0;
    http = strcasecmp(scheme, "http") == 0;
    curl_free(scheme);
    curl_url_cleanup(parsed_url);
    if (!https && !http) {
        zget_set_error(ctx, ZGET_EINVAL, "URL must use HTTP or HTTPS");
        return ZGET_EINVAL;
    }
    ctx->curl = curl_easy_init();
    if (ctx->curl == NULL) {
        zget_set_error(ctx, ZGET_ENOMEM, "could not create libcurl handle");
        return ZGET_ENOMEM;
    }
#define SETOPT(opt, value) do { cc = curl_easy_setopt(ctx->curl, opt, value); \
    if (cc != CURLE_OK) goto fail; } while (0)
    /*
     * libcurl does not forward credentials to unrelated hosts unless explicitly
     * requested; zget never enables unrestricted authentication. Protocol lists
     * also prevent redirects from escaping HTTP(S), and HTTPS origins receive
     * the stricter HTTPS-only redirect set to forbid downgrade.
     */
    SETOPT(CURLOPT_FOLLOWLOCATION, 1L);
    SETOPT(CURLOPT_MAXREDIRS, (long)ctx->options.max_redirects);
    SETOPT(CURLOPT_PROTOCOLS_STR, "http,https");
    SETOPT(CURLOPT_REDIR_PROTOCOLS_STR, https ? "https" : "http,https");
    SETOPT(CURLOPT_FAILONERROR, 0L);
    SETOPT(CURLOPT_NOSIGNAL, 1L);
    SETOPT(CURLOPT_USERAGENT, "zget/" ZGET_VERSION_STRING);
    return ZGET_OK;
fail:
    zget_set_error(ctx, ZGET_EHTTP, "could not configure libcurl: %s",
                   curl_easy_strerror(cc));
    return ZGET_EHTTP;
#undef SETOPT
}

int zget_http_read(void *opaque, uint64_t offset, uint64_t length,
                   zget_write_cb cb, void *userdata)
{
    struct zget_ctx *ctx = opaque;
    /* Never turn untrusted ZIP arithmetic directly into a network request. */
    if (ctx->archive_size != 0 && !zget_range_valid(offset, length, ctx->archive_size)) {
        zget_set_error(ctx, ZGET_EZIP, "ZIP range lies outside the archive");
        return ZGET_EZIP;
    }
    return perform_range(ctx, offset, length, false, cb, userdata);
}

int zget_http_read_suffix(struct zget_ctx *ctx, uint64_t length,
                          zget_write_cb cb, void *userdata)
{
    return perform_range(ctx, 0, length, true, cb, userdata);
}
