#include "headers.h"

#include "string_util.h"

namespace datadog::common {

ngx_table_elt_t *search_header(ngx_list_t &headers, std::string_view key) {
  ngx_list_part_t *part = &headers.part;
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
        ngx_strncasecmp((u_char *)key.data(), h[i].key.data, key.size()) != 0) {
      continue;
    }

    return &h[i];
  }

  return nullptr;
}

bool add_header(ngx_pool_t &pool, ngx_list_t &headers, std::string_view key,
                std::string_view value) {
  if (headers.last == nullptr) {
    // Certainly a bad request (4xx). No need to add HTTP headers.
    return false;
  }

  ngx_table_elt_t *h = static_cast<ngx_table_elt_t *>(ngx_list_push(&headers));
  if (h == nullptr) {
    return false;
  }

  const auto key_size = key.size();

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
  h->key.data = (u_char *)ngx_pnalloc(&pool, sizeof(char) * key_size);
  for (std::size_t i = 0; i < key_size; ++i) {
    h->key.data[i] = nginx::to_lower(key[i]);
  }
  h->lowcase_key = h->key.data;

  h->value = nginx::to_ngx_str(&pool, value);
  return true;
}

}  // namespace datadog::common
