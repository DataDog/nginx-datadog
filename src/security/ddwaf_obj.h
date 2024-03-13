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

namespace datadog::nginx::security {

struct may_alias ddwaf_str_obj;
struct may_alias ddwaf_arr_obj;
struct may_alias ddwaf_map_obj;

struct may_alias ddwaf_obj : ddwaf_object {
  using nb_entries_t = decltype(nbEntries);
  ddwaf_obj() = default;
  explicit ddwaf_obj(const ddwaf_object &dobj) : ddwaf_object{dobj} {}

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
    }

    if (type == DDWAF_OBJ_UNSIGNED) {
      static constexpr auto max = std::numeric_limits<T>::max();
      if (uintValue > max) {
        throw std::out_of_range("value out of range");
      }
      return static_cast<T>(uintValue);
    }

    if (type == DDWAF_OBJ_FLOAT) {
      if (f64 < min || f64 > max) {
        throw std::out_of_range("value out of range");
      }
      return static_cast<T>(f64);
    }

    throw std::invalid_argument("not a numeric value");
  }

  ddwaf_str_obj &make_string(std::string_view sv) {
    type = DDWAF_OBJ_STRING;
    stringValue = const_cast<char *>(sv.data()); // NOLINT
    nbEntries = sv.length();
    return *reinterpret_cast<ddwaf_str_obj *>(this); // NOLINT
  }

  ddwaf_arr_obj &make_array(ddwaf_obj *arr, nb_entries_t size) {
    type = DDWAF_OBJ_ARRAY;
    array = arr;
    nbEntries = size;
    return *reinterpret_cast<ddwaf_arr_obj *>(this); // NOLINT
  }
  ddwaf_arr_obj &make_array(nb_entries_t size, ddwaf_memres &memres) {
    type = DDWAF_OBJ_ARRAY;
    array = memres.allocate_objects(size);
    nbEntries = size;
    return *reinterpret_cast<ddwaf_arr_obj *>(this); // NOLINT
  }

  ddwaf_map_obj &make_map(ddwaf_obj *entries, nb_entries_t size) {
    type = DDWAF_OBJ_MAP;
    array = entries;
    nbEntries = size;
    return *reinterpret_cast<ddwaf_map_obj *>(this); // NOLINT
  }
  ddwaf_map_obj &make_map(nb_entries_t size, ddwaf_memres &memres) {
    type = DDWAF_OBJ_MAP;
    array = memres.allocate_objects(size);
    nbEntries = size;
    return *reinterpret_cast<ddwaf_map_obj *>(this); // NOLINT
  }
};

struct may_alias ddwaf_str_obj : ddwaf_obj {
  explicit ddwaf_str_obj(const ddwaf_object &dobj) : ddwaf_obj(dobj) {
    if (dobj.type != DDWAF_OBJ_STRING) {
      throw std::invalid_argument("not a string");
    }
  }

  std::string_view value() const {
    return std::string_view{stringValue, nbEntries};
  }
};

struct may_alias ddwaf_arr_obj : ddwaf_obj {
  // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
  ddwaf_arr_obj(const ddwaf_object &dobj) : ddwaf_obj(dobj) {
    if (dobj.type != DDWAF_OBJ_ARRAY) {
      throw std::invalid_argument("not an array");
    }
  }

  template <typename T = ddwaf_object>
  T &at_unchecked(nb_entries_t index) const {
    static_assert(std::is_base_of<ddwaf_obj, T>::value,
                  "T must be a subclass of ddwaf_obj");
    return *reinterpret_cast<T*>(&array[index]); // NOLINT
  }

  template <typename T = ddwaf_object>
  T &at(nb_entries_t index) const {
    if (index >= nbEntries) {
      throw std::out_of_range("index out of range");
    }
    return at_unchecked<T>(index);
  }

  struct iterator {
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
    iterator(const ddwaf_arr_obj &arr, nb_entries_t index = 0)
        : arr_{arr}, index_{index} {}

    bool operator!=(const iterator &other) const {
      return index_ != other.index_ || &arr_ != &other.arr_;
    }

    iterator &operator++() {
      index_++;
      return *this;
    }

    ddwaf_obj operator*() const { return arr_.at<ddwaf_obj>(index_); }

   private:
   // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const ddwaf_arr_obj &arr_;
    nb_entries_t index_;
  };

  iterator begin() const { return {*this}; }
  iterator end() const { return {*this, nbEntries}; }
};

struct ddwaf_map_obj : ddwaf_obj {
  explicit ddwaf_map_obj(const ddwaf_object &dobj) : ddwaf_obj(dobj) {
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

  template <typename T = ddwaf_obj>
  std::optional<T> get_opt(std::string_view key) const {
    for (nb_entries_t i = 0; i < nbEntries; i++) {
      ddwaf_obj dobj{array[i]};
      if (dobj.key() == key) {
        return T{dobj};
      }
    }
    return std::nullopt;
  }

  template<typename T = ddwaf_obj>
  T& get_entry_unchecked(std::size_t i) {
    static_assert(std::is_base_of_v<ddwaf_obj, T>,
                  "T must be a subclass of ddwaf_obj");
    return *reinterpret_cast<T*>(&array[i]); // NOLINT
  }

  struct iterator {
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
    iterator(const ddwaf_map_obj &map, nb_entries_t index = 0)
        : map_{map}, index_{index} {}

    bool operator!=(const iterator &other) const {
      return index_ != other.index_ || &map_ != &other.map_;
    }

    iterator &operator++() {
      index_++;
      return *this;
    }

    std::pair<std::string_view, ddwaf_obj> operator*() const {
      ddwaf_obj const dobj{map_.array[index_]};
      return {dobj.key(), dobj};
    }

   private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const ddwaf_map_obj &map_;
    nb_entries_t index_;
  };

  iterator begin() const { return {*this}; }
  iterator end() const { return {*this, nbEntries}; }

  static ddwaf_map_obj empty() {
    return {};
  }

 private:
  ddwaf_map_obj() : ddwaf_obj{} { type = DDWAF_OBJ_MAP; }
};

} // namespace datadog::nginx::security
