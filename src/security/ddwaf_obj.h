#pragma once

#include <ddwaf.h>

#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ddwaf_memres.h"

#define may_alias __attribute__((__may_alias__))

namespace datadog {
namespace nginx {
namespace security {

struct may_alias ddwaf_str_obj;
struct may_alias ddwaf_arr_obj;
struct may_alias ddwaf_map_obj;

struct may_alias ddwaf_obj : ddwaf_object {
  using nb_entries_t = decltype(nbEntries);
  ddwaf_obj() = default;
  ddwaf_obj(const ddwaf_object &dobj) : ddwaf_object{dobj} {}

  std::string_view key() const noexcept {
    return std::string_view{parameterName, parameterNameLength};
  }

  // string lifetime must be at least that of the object
  void set_key(std::string_view sv) noexcept {
    parameterName = sv.data();
    parameterNameLength = sv.length();
  }

  std::string_view string_val_unchecked() const noexcept {
    return std::string_view{stringValue, nbEntries};
  }

  bool is_numeric() const noexcept {
    return type == DDWAF_OBJ_SIGNED || type == DDWAF_OBJ_UNSIGNED ||
           type == DDWAF_OBJ_FLOAT;
  }

  template <typename T>
  T numeric_val_unchecked() const {
    static constexpr auto min = std::numeric_limits<T>::min();
    static constexpr auto max = std::numeric_limits<T>::max();
    if (type == DDWAF_OBJ_SIGNED) {
      if (intValue < min || intValue > max) {
        throw std::out_of_range("value out of range");
      }
      return static_cast<T>(intValue);
    } else if (type == DDWAF_OBJ_UNSIGNED) {
      static constexpr auto max = std::numeric_limits<T>::max();
      if (uintValue > max) {
        throw std::out_of_range("value out of range");
      }
      return static_cast<T>(uintValue);
    } else if (type == DDWAF_OBJ_FLOAT) {
      if (f64 < min || f64 > max) {
        throw std::out_of_range("value out of range");
      }
      return static_cast<T>(f64);
    }
    throw std::invalid_argument("not a numeric value");
  }

  ddwaf_str_obj &make_string(std::string_view sv) {
    type = DDWAF_OBJ_STRING;
    stringValue = const_cast<char *>(sv.data());
    nbEntries = sv.length();
    return *reinterpret_cast<ddwaf_str_obj *>(this);
  }

  template<typename C>
  ddwaf_str_obj &make_string(C *) = delete;

  ddwaf_arr_obj &make_array(ddwaf_obj *arr, nb_entries_t size) {
    type = DDWAF_OBJ_ARRAY;
    array = arr;
    nbEntries = size;
    return *reinterpret_cast<ddwaf_arr_obj *>(this);
  }
  ddwaf_arr_obj &make_array(nb_entries_t size, ddwaf_memres &memres) {
    type = DDWAF_OBJ_ARRAY;
    array = memres.allocate_objects(size);
    nbEntries = size;
    return *reinterpret_cast<ddwaf_arr_obj *>(this);
  }

  ddwaf_map_obj &make_map(ddwaf_obj *entries, nb_entries_t size) {
    type = DDWAF_OBJ_MAP;
    array = entries;
    nbEntries = size;
    return *reinterpret_cast<ddwaf_map_obj *>(this);
  }
  ddwaf_map_obj &make_map(nb_entries_t size, ddwaf_memres &memres) {
    type = DDWAF_OBJ_MAP;
    array = memres.allocate_objects(size);
    nbEntries = size;
    return *reinterpret_cast<ddwaf_map_obj *>(this);
  }
};

struct ddwaf_str_obj : ddwaf_obj {
  ddwaf_str_obj(const ddwaf_object &dobj) : ddwaf_obj(dobj) {
    if (dobj.type != DDWAF_OBJ_STRING) {
      throw std::invalid_argument("not a string");
    }
  }

  std::string_view value() const {
    return std::string_view{stringValue, nbEntries};
  }
};

struct ddwaf_arr_obj : ddwaf_obj {
  ddwaf_arr_obj(const ddwaf_object &dobj) : ddwaf_obj(dobj) {
    if (dobj.type != DDWAF_OBJ_ARRAY) {
      throw std::invalid_argument("not an array");
    }
  }

  template <typename T = ddwaf_object>
  T &at_unchecked(nb_entries_t index) const {
    static_assert(std::is_base_of<ddwaf_obj, T>::value,
                  "T must be a subclass of ddwaf_obj");
    return *reinterpret_cast<T*>(&array[index]);
  }

  template <typename T = ddwaf_object>
  T &at(nb_entries_t index) const {
    if (index >= nbEntries) {
      throw std::out_of_range("index out of range");
    }
    return at_unchecked<T>(index);
  }

  struct iterator {
    iterator(const ddwaf_arr_obj &arr, nb_entries_t index = 0)
        : arr_{arr}, index_{index} {}

    bool operator!=(const iterator &other) const {
      return index_ != other.index_ ||
             std::memcmp(&arr_, &other.arr_, sizeof(arr_)) != 0;
    }

    iterator &operator++() {
      index_++;
      return *this;
    }

    ddwaf_obj operator*() const { return arr_.at<ddwaf_obj>(index_); }

   private:
    const ddwaf_arr_obj &arr_;
    nb_entries_t index_;
  };

  iterator begin() const { return {*this}; }
  iterator end() const { return {*this, nbEntries}; }
};

struct ddwaf_map_obj : ddwaf_obj {
  ddwaf_map_obj(const ddwaf_object &dobj) : ddwaf_obj(dobj) {
    if (dobj.type != DDWAF_OBJ_MAP) {
      throw std::invalid_argument("not a map");
    }
  }

  template <typename T>
  T get(std::string_view key) const {
    static_assert(std::is_base_of<ddwaf_obj, T>::value,
                  "T must be a subclass of ddwaf_obj");
    for (nb_entries_t i = 0; i < nbEntries; i++) {
      ddwaf_obj dobj{array[i]};
      if (dobj.key() == key) {
        return T{dobj};
      }
    }
    throw std::out_of_range(std::string{"key "} + std::string{key} +
                            " not found");
  }

  template <typename T>
  std::optional<T> get_opt(std::string_view key) const {
    for (nb_entries_t i = 0; i < nbEntries; i++) {
      ddwaf_obj dobj{array[i]};
      if (dobj.key() == key) {
        return {dobj};
      }
    }
    return std::nullopt;
  }

  template<typename T = ddwaf_obj>
  T& get_entry_unchecked(std::size_t i) {
    return *reinterpret_cast<T*>(&array[i]);
  }

  struct iterator {
    iterator(const ddwaf_map_obj &map, nb_entries_t index = 0)
        : map_{map}, index_{index} {}

    bool operator!=(const iterator &other) const {
      return index_ != other.index_ ||
             std::memcmp(&map_, &other.map_, sizeof(map_)) != 0;
    }

    iterator &operator++() {
      index_++;
      return *this;
    }

    std::pair<std::string_view, ddwaf_obj> operator*() const {
      ddwaf_obj dobj{map_.array[index_]};
      return {dobj.key(), dobj};
    }

   private:
    const ddwaf_map_obj &map_;
    nb_entries_t index_;
  };

  iterator begin() const { return {*this}; }
  iterator end() const { return {*this, nbEntries}; }
};

}  // namespace security
}  // namespace nginx
}  // namespace datadog
