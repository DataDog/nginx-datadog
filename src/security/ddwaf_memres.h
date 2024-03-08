#pragma once

#include <ddwaf.h>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

extern "C" {
#include <ngx_core.h>
}

namespace datadog {
namespace nginx {
namespace security {

class ddwaf_memres {
  static inline constexpr std::size_t MIN_OBJ_SEG_SIZE = 20;
  static inline constexpr std::size_t MIN_STR_SEG_SIZE = 512;

 public:
  ddwaf_memres() = default;
  ddwaf_memres(const ddwaf_memres &) = delete;
  ddwaf_memres &operator=(const ddwaf_memres &) = delete;
  ddwaf_memres(ddwaf_memres &&) = default;
  ddwaf_memres &operator=(ddwaf_memres &&) = default;

  template <typename T = ddwaf_object>
  T *allocate_objects(std::size_t num_objects) {
    static_assert(sizeof(T) == sizeof(ddwaf_object) &&
                      alignof(T) == alignof(ddwaf_object),
                  "T must have size and alignment of ddwaf_object");
    static_assert(std::is_standard_layout<T>::value, "T must be a POD type");
    static_assert(std::is_base_of<ddwaf_object, T>::value,
                  "T must be derived from ddwaf_object");

    if (num_objects == 0) {
      return nullptr;
    }

    if (objects_stored_ + num_objects >= cur_object_seg_size_) {
      std::size_t size = std::max(MIN_OBJ_SEG_SIZE, num_objects);
      new_objects_segment(size);
    }
    auto *p = allocs_object_.back() + (objects_stored_);

    objects_stored_ += num_objects;
    // keep braces, some code depends on this being zero-initialized:
    return new (p) T[num_objects]{};
  }

  char *allocate_string(size_t len) {
    if (strings_stored_ + len >= cur_string_seg_size_) {
      std::size_t size = std::max(MIN_STR_SEG_SIZE, len);
      new_strings_segment(size);
    }
    char *p = allocs_string_.back() + strings_stored_;

    strings_stored_ += len;

    return p;
  }

  void set_pool(ngx_pool_t &pool) { pool_ = &pool; }

 private:
  void new_objects_segment(size_t num_objects) {
    if (!pool_) {
      throw std::runtime_error("ddwaf_memres: pool not set");
    }

    auto *block = ngx_palloc(pool_, sizeof(ddwaf_object) * num_objects);
    if (!block) {
      throw std::bad_alloc{};
    }
    allocs_object_.emplace_back(new (block) ddwaf_object[num_objects]);
    cur_object_seg_size_ = num_objects;
    objects_stored_ = 0;
  }

  void new_strings_segment(size_t size) {
    if (!pool_) {
      throw std::runtime_error("ddwaf_memres: pool not set");
    }

    auto *block = ngx_palloc(pool_, size);
    allocs_string_.emplace_back(new (block) char[size]);
    cur_string_seg_size_ = size;
    strings_stored_ = 0;
  }

  ngx_pool_t *pool_;
  std::size_t cur_object_seg_size_{0};  // in num objects
  std::size_t cur_string_seg_size_{0};  // in bytes
  std::vector<ddwaf_object*> allocs_object_;
  std::vector<char*> allocs_string_;
  std::size_t objects_stored_{0};
  std::size_t strings_stored_{0};
};

}  // namespace security
}  // namespace nginx
}  // namespace datadog
