#pragma once

#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>

#include "../string_util.h"

extern "C" {
#include <ngx_config.h>
#include <ngx_core.h>
}

namespace datadog::nginx::security {

template <typename T, typename FreeFunc>
struct freeable_resource {
  static_assert(std::is_standard_layout<T>::value, "T must be a POD type");

  T resource;
  freeable_resource() = delete;
  explicit freeable_resource(const T resource) : resource{resource} {}

  freeable_resource(freeable_resource &&other) noexcept
      : resource{other.resource} {
    other.resource = {};
  };
  freeable_resource &operator=(freeable_resource &&other) noexcept {
    if (this != &other) {
      FreeFunc()(resource);
      resource = other.resource;
      other.resource = {};
    }
    return *this;
  }
  freeable_resource(const freeable_resource &other) = delete;
  freeable_resource &operator=(const freeable_resource &other) = delete;
  T &operator*() { return resource; }
  T &get() { return **this; }

  ~freeable_resource() { FreeFunc()(resource); }
};

struct ngx_str_hash {
  std::size_t operator()(const ngx_str_t &str) const noexcept {
    // could use ngx_hash_key(str.data, str.len) but this way it can be inlined
    std::size_t hash = 5381;
    for (std::size_t i = 0; i < str.len; ++i) {
      hash = ((hash << 5) + hash) + str.data[i];  // hash * 33 + c
    }
    return hash;
  }
};

struct ngx_str_equal {
  bool operator()(const ngx_str_t &lhs, const ngx_str_t &rhs) const {
    if (lhs.len != rhs.len) {
      return false;
    }
    return std::memcmp(lhs.data, rhs.data, lhs.len) == 0;
  }
};

constexpr inline ngx_uint_t ngx_hash_ce(std::string_view sv) {
  ngx_uint_t key{};
  for (std::size_t i = 0; i < sv.length(); i++) {
    key = ngx_hash(key, sv.at(i));
  }

  return key;
}

inline std::string_view key(const ngx_table_elt_t &header) {
  return to_string_view(header.key);
}

inline std::string_view lc_key(const ngx_table_elt_t &header) {
  return {reinterpret_cast<const char *>(header.lowcase_key), header.key.len};
}
inline bool key_equals_ci(const ngx_table_elt_t &header, std::string_view key) {
  if (header.lowcase_key) {
    return key == lc_key(header);
  }

  if (header.key.len != key.size()) {
    return false;
  }

  for (std::size_t i = 0; i < header.key.len; ++i) {
    if (std::tolower(header.key.data[i]) != key[i]) {
      return false;
    }
  }
  return true;
}

template <typename T>
class nginx_list_iter {
  explicit nginx_list_iter(const ngx_list_part_t *part, ngx_uint_t index)
      : part_{part},
        elts_{static_cast<T *>(part_ ? part_->elts : nullptr)},
        index_{index} {}

 public:
  using difference_type = void;
  using value_type = T;
  using pointer = T *;
  using reference = T &;
  using iterator_category = std::forward_iterator_tag;

  explicit nginx_list_iter(const ngx_list_t &list)
      : nginx_list_iter{&list.part, 0} {}

  bool operator!=(const nginx_list_iter &other) const {
    return part_ != other.part_ || index_ != other.index_;
  }

  bool operator==(const nginx_list_iter &other) const {
    return !(*this == other);
  }

  nginx_list_iter &operator++() {
    ++index_;
    while (index_ >= part_->nelts) {
      if (part_->next == nullptr) {
        return *this;  // reached the end
      }

      part_ = part_->next;
      elts_ = static_cast<T *>(part_ ? part_->elts : nullptr);
      index_ = 0;  // if the part is empty, we go for another iteration
    }
    return *this;
  }

  T &operator*() const { return elts_[index_]; }

  static nginx_list_iter<T> end(const ngx_list_t &list) {
    return nginx_list_iter{list.last, list.last ? list.last->nelts : 0};
  }

 private:
  const ngx_list_part_t *part_;
  T *elts_;  // part_->elts, after cast
  ngx_uint_t index_{};
};

class ngnix_header_iterable {
 public:
  explicit ngnix_header_iterable(const ngx_list_t &list) : list_{list} {}

  nginx_list_iter<ngx_table_elt_t> begin() {
    return nginx_list_iter<ngx_table_elt_t>{list_};
  }

  nginx_list_iter<ngx_table_elt_t> end() {
    return nginx_list_iter<ngx_table_elt_t>::end(list_);
  }

 private:
  const ngx_list_t &list_;  // NOLINT
};

inline ngx_str_t ngx_stringv(std::string_view sv) noexcept {
  return {sv.size(),  // NOLINTNEXTLINE
          const_cast<u_char *>(reinterpret_cast<const u_char *>(sv.data()))};
}

}  // namespace datadog::nginx::security
