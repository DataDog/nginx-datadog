#include "headers.h"

#include "string_util.h"

namespace datadog::common {

ngx_table_elt_t* search_header(ngx_list_t& headers, std::string_view key) {
  ngx_list_part_t* part = &headers.part;
  auto* h = static_cast<ngx_table_elt_t*>(part->elts);

  for (std::size_t i = 0;; i++) {
    if (i >= part->nelts) {
      if (part->next == nullptr) {
        break;
      }

      part = part->next;
      h = static_cast<ngx_table_elt_t*>(part->elts);
      i = 0;
    }

    if (key.size() != h[i].key.len ||
        ngx_strncasecmp((u_char*)key.data(), h[i].key.data, key.size()) != 0) {
      continue;
    }

    return &h[i];
  }

  return nullptr;
}

bool add_header(ngx_pool_t& pool, ngx_list_t& headers, std::string_view key,
                std::string_view value) {
  if (headers.last == nullptr) {
    // Certainly a bad request (4xx). No need to add HTTP headers.
    return false;
  }

  ngx_table_elt_t* h = static_cast<ngx_table_elt_t*>(ngx_list_push(&headers));
  if (h == nullptr) {
    return false;
  }

  const auto key_size = key.size();

  // HTTP proxy module expects the header to has a lowercased key value
  // Instead of allocating twice the same key, `h->key` and `h->lowcase_key`
  // use the same data.
  h->key.len = key_size;
  h->key.data = (u_char*)ngx_pnalloc(&pool, sizeof(char) * key_size);
  for (std::size_t i = 0; i < key_size; ++i) {
    h->key.data[i] = nginx::to_lower(key[i]);
  }
  h->lowcase_key = h->key.data;

  // In request headers, the hash should be calculated from the lowercase key.
  // See ngx_http_parse_header_line in ngx_http_parse.c
  // Response headers OTOH use either 1 or 0, with 0 meaning "skip this header".
  h->hash = ngx_hash_key(h->lowcase_key, key.size());

  h->value = nginx::to_ngx_str(&pool, value);
  return true;
}

bool remove_header(ngx_list_t& headers, std::string_view key) {
  auto key_lc = std::unique_ptr<u_char[]>{new u_char[key.size()]};
  std::transform(key.begin(), key.end(), key_lc.get(),
                 datadog::nginx::to_lower);
  ngx_uint_t key_hash = ngx_hash_key(key_lc.get(), key.size());

  ngx_list_part_t* part = &headers.part;
  ngx_table_elt_t* h = static_cast<ngx_table_elt_t*>(part->elts);
  for (std::size_t i = 0;; i++) {
    if (i >= part->nelts) {
      if (part->next == nullptr) {
        break;
      }

      part = part->next;
      h = static_cast<ngx_table_elt_t*>(part->elts);
      i = 0;
    }

    if (h[i].hash != key_hash || key.size() != h[i].key.len ||
        memcmp(key_lc.get(), h[i].lowcase_key, key.size()) != 0) {
      continue;
    }

    part->nelts--;
    if (i < part->nelts) {
      memmove(&h[i], &h[i + 1], (part->nelts - i) * sizeof(*h));
    }
    return true;
  }

  return false;
}

}  // namespace datadog::common
