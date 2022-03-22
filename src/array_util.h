#pragma once

extern "C" {
#include <ngx_core.h>
}

namespace datadog {
namespace nginx {

// Apply `f` to each element of an ngx_list_t.
template <class T, class F>
void for_each(const ngx_list_t &list, F f) {
  auto part = &list.part;
  auto elements = static_cast<T *>(part->elts);
  for (ngx_uint_t i = 0;; i++) {
    if (i >= part->nelts) {
      if (!part->next) return;
      part = part->next;
      elements = static_cast<T *>(part->elts);
      i = 0;
    }
    f(elements[i]);
  }
}

// Apply `f` to each element of an ngx_array_t.
template <class T, class F>
void for_each(const ngx_array_t &array, F f) {
  auto elements = static_cast<T *>(array.elts);
  auto n = array.nelts;
  for (size_t i = 0; i < n; ++i) f(elements[i]);
}

}  // namespace nginx
}  // namespace datadog
