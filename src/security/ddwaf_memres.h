#pragma once

#include <ddwaf.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <vector>

extern "C" {
#include <ngx_core.h>
}

namespace datadog::nginx::security {

class DdwafMemres {
  static inline constexpr std::size_t kMinObjSegSize = 20;
  static inline constexpr std::size_t kMinStrSegSize = 512;

 public:
  DdwafMemres() = default;
  DdwafMemres(const DdwafMemres &) = delete;
  DdwafMemres &operator=(const DdwafMemres &) = delete;
  DdwafMemres(DdwafMemres &&) = default;
  DdwafMemres &operator=(DdwafMemres &&) = default;
  ~DdwafMemres() = default;

  template <typename T = ddwaf_object>
  T *allocate_objects(std::size_t num_objects) {
    // NOLINTNEXTLINE(misc-redundant-expression)
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
      std::size_t const size = std::max(kMinObjSegSize, num_objects);
      new_objects_segment(size);
    }
    auto *p = allocs_object_.back().get() + (objects_stored_);

    objects_stored_ += num_objects;
    // keep braces, some code depends on this being zero-initialized:
    return new (p) T[num_objects]{};  // NOLINT(cppcoreguidelines-owning-memory)
  }

  char *allocate_string(size_t len) {
    if (strings_stored_ + len >= cur_string_seg_size_) {
      std::size_t const size = std::max(kMinStrSegSize, len);
      new_strings_segment(size);
    }
    char *p = allocs_string_.back().get() + strings_stored_;

    strings_stored_ += len;

    return p;
  }

  void clear() {
    cur_object_seg_size_ = 0;
    cur_string_seg_size_ = 0;
    objects_stored_ = 0;
    strings_stored_ = 0;
    allocs_object_.clear();
    allocs_string_.clear();
  }

 private:
  void new_objects_segment(size_t num_objects) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    allocs_object_.emplace_back(new ddwaf_object[num_objects]);
    cur_object_seg_size_ = num_objects;
    objects_stored_ = 0;
  }

  void new_strings_segment(size_t size) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    allocs_string_.emplace_back(new char[size]);
    cur_string_seg_size_ = size;
    strings_stored_ = 0;
  }

  std::size_t cur_object_seg_size_{0};  // in num objects
  std::size_t cur_string_seg_size_{0};  // in bytes
  std::vector<std::unique_ptr<ddwaf_object[]>> allocs_object_;
  std::vector<std::unique_ptr<char[]>> allocs_string_;
  std::size_t objects_stored_{0};
  std::size_t strings_stored_{0};
};

/*
 * A pool of ddwaf object vectors of sizes in powers in two. Used when the
 * sizes of arrays or maps are not known in advance.
 */
template <typename DdwafObjType>
requires std::is_base_of_v<ddwaf_object, DdwafObjType>
class DdwafObjArrPool {
 public:
  DdwafObjArrPool(DdwafMemres &memres) : memres_{memres} {}

  DdwafObjType *alloc(std::size_t size) {
    auto it = free_.find(size);
    if (it != free_.end()) {
      std::vector<DdwafObjType *> &free_list = it->second;
      if (!free_list.empty()) {
        auto *obj = free_list.back();
        free_list.pop_back();
        return new (obj) DdwafObjType[size]{};
      }
    }

    return memres_.allocate_objects<DdwafObjType>(size);
  }

  DdwafObjType *realloc(DdwafObjType *arr, std::size_t cur_size,
                        std::size_t new_size) {
    assert(new_size > cur_size);
    auto *new_arr = alloc(new_size);
    if (cur_size > 0) {
      std::copy_n(arr, cur_size, new_arr);

      std::vector<DdwafObjType *> &free_list = free_[cur_size];
      free_list.emplace_back(arr);
    }

    return new_arr;
  }

 private:
  DdwafMemres &memres_;
  std::unordered_map<std::size_t, std::vector<DdwafObjType *>> free_;
};

}  // namespace datadog::nginx::security
