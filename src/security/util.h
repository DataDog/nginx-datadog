#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

extern "C" {
    #include <ngx_core.h>
}

namespace datadog {
namespace nginx {
namespace security {

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

template <typename T>
class nginx_list_iter {
 public:
  explicit nginx_list_iter(const ngx_list_t &list)
      : nginx_list_iter{list.last} {}

  nginx_list_iter(ngx_list_part_t *part)
      : part_{part},
        elts_{static_cast<T *>(part_ ? part_->elts : nullptr)},
        index_{0} {}

  bool operator!=(const nginx_list_iter &other) const {
    return part_ != other.part_ || index_ != other.index_;
  }

  nginx_list_iter &operator++() {
    if (!part_) {
      return *this;
    }

    ++index_;
    if (index_ >= part_->nelts) {
      part_ = part_->next;
      elts_ = static_cast<T *>(part_ ? part_->elts : nullptr);
      index_ = 0;
    }
    return *this;
  }

  T &operator*() { return elts_[index_]; }

  static nginx_list_iter<T> end() { return nginx_list_iter{nullptr}; }

 private:
  ngx_list_part_t *part_;
  T *elts_;  // part_->elts, after cast
  ngx_uint_t index_;
};

class ngnix_header_iterable {
 public:
  explicit ngnix_header_iterable(const ngx_list_t &list) : list_{list} {}

  nginx_list_iter<ngx_table_elt_t> begin() {
    return nginx_list_iter<ngx_table_elt_t>{list_};
  }

  nginx_list_iter<ngx_table_elt_t> end() {
    return nginx_list_iter<ngx_table_elt_t>::end();
  }

 private:
  const ngx_list_t &list_;
};

inline ngx_str_t ngx_stringv(std::string_view sv) noexcept {
  return {sv.size(),
          const_cast<u_char *>(reinterpret_cast<const u_char *>(sv.data()))};
}

inline std::string_view to_sv(const ngx_str_t &str) noexcept {
  return {reinterpret_cast<const char *>(str.data), str.len};
}

}  // namespace security
}  // namespace nginx
}  // namespace datadog
