#include <ngx_core.h>
#include <ngx_http.h>
#include <string.h>
#include <stdlib.h>

void ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
                        const char *fmt, ...) {}

ngx_uint_t ngx_hash_key(u_char *data, size_t len) {
    ngx_uint_t  i, key;

    key = 0;

    for (i = 0; i < len; i++) {
        key = ngx_hash(key, data[i]);
    }

    return key;
}

void *ngx_palloc(ngx_pool_t *pool, size_t size) {
    (void)pool;
    return malloc(size);
}

u_char *ngx_snprintf(u_char *buf, size_t max, const char *fmt, ...) {
    (void)buf;
    (void)max;
    (void)fmt;
    return buf;
}

char *ngx_conf_parse(ngx_conf_t *cf, ngx_str_t *filename) {
    (void)cf;
    (void)filename;
    return NGX_CONF_OK;
}

ngx_int_t ngx_http_parse_uri(ngx_http_request_t *r) {
    (void)r;
    return NGX_OK;
}
