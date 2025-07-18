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
struct FreeableResource {
  static_assert(std::is_standard_layout<T>::value, "T must be a POD type");

  T resource;
  FreeableResource() = delete;
  explicit FreeableResource(const T resource) : resource{resource} {}

  FreeableResource(FreeableResource &&other) noexcept
      : resource{other.resource} {
    other.resource = {};
  };
  FreeableResource &operator=(FreeableResource &&other) noexcept {
    if (this != &other) {
      FreeFunc()(resource);
      resource = other.resource;
      other.resource = {};
    }
    return *this;
  }
  FreeableResource(const FreeableResource &other) = delete;
  FreeableResource &operator=(const FreeableResource &other) = delete;
  T &operator*() { return resource; }
  T &get() { return **this; }
  const T &get() const { return **this; }

  ~FreeableResource() { FreeFunc()(resource); }
};

struct NgxStrHash {
  std::size_t operator()(const ngx_str_t &str) const noexcept {
    // could use ngx_hash_key(str.data, str.len) but this way it can be inlined
    std::size_t hash = 5381;
    for (std::size_t i = 0; i < str.len; ++i) {
      hash = ((hash << 5) + hash) + str.data[i];  // hash * 33 + c
    }
    return hash;
  }
};

struct NgxStrEqual {
  bool operator()(const ngx_str_t &lhs, const ngx_str_t &rhs) const {
    return lhs.len == rhs.len && std::memcmp(lhs.data, rhs.data, lhs.len) == 0;
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

inline bool req_key_equals_ci(const ngx_table_elt_t &header,
                              std::string_view key) {
#if NGX_DEBUG
  for (std::size_t i = 0; i < key.length(); i++) {
    if (std::tolower(key[i]) != key[i]) {
      throw new std::invalid_argument("key must be lowercase");
    }
  }
#endif

  return key == lc_key(header);
}

inline bool resp_key_equals_ci(const ngx_table_elt_t &header,
                               std::string_view key) {
#if NGX_DEBUG
  for (std::size_t i = 0; i < key.length(); i++) {
    if (std::tolower(key[i]) != key[i]) {
      throw new std::invalid_argument("key must be lowercase");
    }
  }
#endif

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
class NginxListIter {
  explicit NginxListIter(const ngx_list_part_t *part, ngx_uint_t index)
      : part_{part},
        elts_{static_cast<T *>(part_ ? part_->elts : nullptr)},
        index_{index} {}

 public:
  // NOLINTBEGIN(readability-identifier-naming)
  using difference_type = void;
  using value_type = T;
  using pointer = T *;
  using reference = T &;
  using iterator_category = std::forward_iterator_tag;
  // NOLINTEND(readability-identifier-naming)

  explicit NginxListIter(const ngx_list_t &list)
      : NginxListIter{&list.part, 0} {}

  bool operator!=(const NginxListIter &other) const {
    return part_ != other.part_ || index_ != other.index_;
  }

  bool operator==(const NginxListIter &other) const {
    return !(*this != other);
  }

  NginxListIter &operator++() {
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

  static NginxListIter<T> end(const ngx_list_t &list) {
    return NginxListIter{list.last, list.last ? list.last->nelts : 0};
  }

 private:
  const ngx_list_part_t *part_;
  T *elts_;  // part_->elts, after cast
  ngx_uint_t index_{};
};

class NgnixHeaderIterable {
 public:
  explicit NgnixHeaderIterable(const ngx_list_t &list) : list_{list} {}

  NginxListIter<ngx_table_elt_t> begin() {
    return NginxListIter<ngx_table_elt_t>{list_};
  }

  NginxListIter<ngx_table_elt_t> end() {
    return NginxListIter<ngx_table_elt_t>::end(list_);
  }

 private:
  const ngx_list_t &list_;  // NOLINT
};

inline ngx_str_t ngx_stringv(std::string_view sv) noexcept {
  return {sv.size(),  // NOLINTNEXTLINE
          const_cast<u_char *>(reinterpret_cast<const u_char *>(sv.data()))};
}

namespace chain {
inline std::size_t length(ngx_chain_t const *ch) {
  std::size_t len = 0;
  for (ngx_chain_t const *cl = ch; cl; cl = cl->next) {
    len++;
  }
  return len;
}
inline std::size_t size(ngx_chain_t const *ch) {
  std::size_t size = 0;
  for (ngx_chain_t const *cl = ch; cl; cl = cl->next) {
    size += ngx_buf_size(cl->buf);
  }
  return size;
}
inline std::size_t has_special(ngx_chain_t const *ch) {
  for (ngx_chain_t const *cl = ch; cl; cl = cl->next) {
    return ngx_buf_special(cl->buf);
  }
  return false;
}
inline std::size_t has_last(ngx_chain_t const *ch) {
  for (ngx_chain_t const *cl = ch; cl; cl = cl->next) {
    if (cl->buf->last) {
      return true;
    }
  }
  return false;
}
}  // namespace chain

}  // namespace datadog::nginx::security
