#pragma once

#include <datadog/dict_writer.h>

#include "string_util.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

class NgxHeaderWriter : public datadog::tracing::DictWriter {
  ngx_http_request_t *request_;
  ngx_pool_t *pool_;

 public:
  explicit NgxHeaderWriter(ngx_http_request_t *request)
      : request_(request), pool_(request_->pool) {}

  void set(std::string_view key, std::string_view value) override {
    const auto key_size = key.size();

    ngx_table_elt_t *h = search_header(key);
    if (h != nullptr) {
      h->value = to_ngx_str(pool_, value);
    } else {
      h = static_cast<ngx_table_elt_t *>(
          ngx_list_push(&request_->headers_in.headers));
      if (h == nullptr) {
        return;
      }

      // This trick tells ngx_http_header_module to reflect the header value
      // in the actual response. Otherwise the header will be ignored and client
      // will never see it. To date the value must be just non zero.
      // Source:
      // <https://web.archive.org/web/20240409072840/https://www.nginx.com/resources/wiki/start/topics/examples/headers_management/>
      h->hash = 1;

      // HTTP proxy module expects the header to has a lowercased key value
      // Instead of allocating twice the same key, `h->key` and `h->lowcase_key`
      // use the same data.
      h->key.len = key_size;
      h->key.data = (u_char *)ngx_pnalloc(pool_, sizeof(char) * key_size);
      for (std::size_t i = 0; i < key_size; ++i) {
        h->key.data[i] = to_lower(key[i]);
      }
      h->lowcase_key = h->key.data;

      h->value = to_ngx_str(pool_, value);
    }
  }

 private:
  ngx_table_elt_t *search_header(std::string_view key) {
    ngx_list_part_t *part = &request_->headers_in.headers.part;
    auto *h = static_cast<ngx_table_elt_t *>(part->elts);

    for (std::size_t i = 0;; i++) {
      if (i >= part->nelts) {
        if (part->next == nullptr) {
          break;
        }

        part = part->next;
        h = static_cast<ngx_table_elt_t *>(part->elts);
        i = 0;
      }

      if (key.size() != h[i].key.len ||
          ngx_strcasecmp((u_char *)key.data(), h[i].key.data) != 0) {
        continue;
      }

      return &h[i];
    }

    return nullptr;
  }
};

}  // namespace nginx
}  // namespace datadog
