#pragma once

#include <ddwaf.h>

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

extern "C" {
#include <ngx_core.h>
}

namespace datadog::nginx::security {

// Defined in ddwaf_obj.h, which includes this header
struct ddwaf_kv_obj;

// Types with the layout of a ddwaf_object. The arena never runs
// destructors, so they must be trivially destructible.
template <typename T>
concept DdwafObjLike = std::is_standard_layout_v<T> &&
                           std::is_trivially_destructible_v<T> &&
                           sizeof(T) == sizeof(ddwaf_object) &&
                       alignof(T) == alignof(ddwaf_object);

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

  template <DdwafObjLike T>
  T *allocate_objects(std::size_t num_objects) {
    if (num_objects == 0) {
      return nullptr;
    }
    void *p = allocate_object_storage(num_objects);
    // ddwaf_obj's default constructor gives each object the type
    // DDWAF_OBJ_INVALID (0). Its other bytes are indeterminate.
    return new (p) T[num_objects]{};  // NOLINT(cppcoreguidelines-owning-memory)
  }

  // A map entry (key + value) takes the space of two objects.
  // A template only because ddwaf_kv_obj is incomplete here; ddwaf_obj.h
  // checks its layout.
  template <std::same_as<ddwaf_kv_obj> KV = ddwaf_kv_obj>
  KV *allocate_kvs(std::size_t num_entries) {
    if (num_entries == 0) {
      return nullptr;
    }
    void *p = allocate_object_storage(2 * num_entries);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return new (p) KV[num_entries]{};
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

  // Copies the string into the arena.
  std::string_view copy_string(std::string_view sv) {
    char *s = allocate_string(sv.size());
    std::memcpy(s, sv.data(), sv.size());
    return {s, sv.size()};
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
  // Raw storage for num_objects objects. The caller creates them.
  void *allocate_object_storage(std::size_t num_objects) {
    if (objects_stored_ + num_objects >= cur_object_seg_size_) {
      std::size_t const size = std::max(kMinObjSegSize, num_objects);
      new_objects_segment(size);
    }
    std::byte *p =
        allocs_object_.back().get() + objects_stored_ * sizeof(ddwaf_object);
    objects_stored_ += num_objects;
    return p;
  }

  void new_objects_segment(size_t num_objects) {
    // new[] aligns to at least __STDCPP_DEFAULT_NEW_ALIGNMENT__
    static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= alignof(ddwaf_object));
    allocs_object_.emplace_back(
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        new std::byte[num_objects * sizeof(ddwaf_object)]);
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
  // std::byte arrays provide storage for the objects created in them
  std::vector<std::unique_ptr<std::byte[]>> allocs_object_;
  std::vector<std::unique_ptr<char[]>> allocs_string_;
  std::size_t objects_stored_{0};
  std::size_t strings_stored_{0};
};

/*
 * A pool of ddwaf object vectors of sizes in powers in two. Used when the
 * sizes of arrays or maps are not known in advance.
 */
template <DdwafObjLike DdwafObjType>
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
