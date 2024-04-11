#pragma once

#include <ddwaf.h>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ddwaf_memres.h"
#include "util.h"

namespace datadog::nginx::security {

struct __attribute__((__may_alias__)) ddwaf_str_obj;
struct __attribute__((__may_alias__)) ddwaf_arr_obj;
struct __attribute__((__may_alias__)) ddwaf_map_obj;

// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) ddwaf_obj : ddwaf_object {
  using nb_entries_t = decltype(nbEntries);  // NOLINT
  ddwaf_obj() : ddwaf_object{} {}
  explicit ddwaf_obj(const ddwaf_object &dobj) : ddwaf_object{dobj} {}

  std::string_view key() const noexcept {
    return std::string_view{parameterName, parameterNameLength};
  }

  // string lifetime must be at least that of the object
  ddwaf_obj &set_key(std::string_view sv) noexcept {
    parameterName = sv.data();
    parameterNameLength = sv.length();
    return *this;
  }

  ddwaf_obj &set_key(std::string_view sv, DdwafMemres &memres) {
    char *s = memres.allocate_string(sv.size());
    std::memcpy(s, sv.data(), sv.size());
    return set_key({s, sv.size()});
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

  ddwaf_obj &make_bool(bool value) {
    type = DDWAF_OBJ_BOOL;
    boolean = value;
    return *this;
  }

  template <typename T>
  ddwaf_obj &make_number(T value) {
    static_assert(std::is_arithmetic_v<T>, "T must be an arithmetic type");
    if constexpr (std::is_floating_point_v<T>) {
      type = DDWAF_OBJ_FLOAT;
      f64 = value;
    } else if constexpr (std::is_signed_v<T>) {
      type = DDWAF_OBJ_SIGNED;
      intValue = value;
    } else {
      type = DDWAF_OBJ_UNSIGNED;
      uintValue = value;
    }
    return *this;
  }

  ddwaf_obj &make_null() {
    type = DDWAF_OBJ_NULL;
    return *this;
  }

  ddwaf_str_obj &make_string(std::string_view sv) {
    type = DDWAF_OBJ_STRING;
    stringValue = const_cast<char *>(sv.data());  // NOLINT
    nbEntries = sv.length();
    return *reinterpret_cast<ddwaf_str_obj *>(this);  // NOLINT
  }

  ddwaf_str_obj &make_string(std::string_view sv, DdwafMemres &memres) {
    char *s = memres.allocate_string(sv.size());
    std::memcpy(s, sv.data(), sv.size());
    return make_string({s, sv.size()});
  }

  template <typename C>
  ddwaf_str_obj &make_string(C *) = delete;

  ddwaf_arr_obj &make_array(ddwaf_obj *arr, nb_entries_t size) {
    type = DDWAF_OBJ_ARRAY;
    array = arr;
    nbEntries = size;
    return *reinterpret_cast<ddwaf_arr_obj *>(this);  // NOLINT
  }
  ddwaf_arr_obj &make_array(nb_entries_t size, DdwafMemres &memres) {
    type = DDWAF_OBJ_ARRAY;
    array = memres.allocate_objects(size);
    nbEntries = size;
    return *reinterpret_cast<ddwaf_arr_obj *>(this);  // NOLINT
  }

  ddwaf_map_obj &make_map(ddwaf_obj *entries, nb_entries_t size) {
    type = DDWAF_OBJ_MAP;
    array = entries;
    nbEntries = size;
    return *reinterpret_cast<ddwaf_map_obj *>(this);  // NOLINT
  }
  ddwaf_map_obj &make_map(nb_entries_t size, DdwafMemres &memres) {
    type = DDWAF_OBJ_MAP;
    array = memres.allocate_objects(size);
    nbEntries = size;
    return *reinterpret_cast<ddwaf_map_obj *>(this);  // NOLINT
  }

  template <typename T>
  T &shallow_copy_val_from(const T &oth) {
    static_assert(std::is_base_of<ddwaf_obj, T>::value,
                  "T must be a subclass of ddwaf_obj");

    // does not copy the key
    std::memcpy(
        reinterpret_cast<char *>(this) + offsetof(ddwaf_obj, stringValue),
        reinterpret_cast<const char *>(&oth) + offsetof(ddwaf_obj, stringValue),
        sizeof(ddwaf_obj) - offsetof(ddwaf_obj, stringValue));
    return reinterpret_cast<T &>(*this);  // NOLINT
  }

  struct Iterator {
    using difference_type = nb_entries_t;                 // NOLINT
    using value_type = ddwaf_obj;                         // NOLINT
    using pointer = value_type *;                         // NOLINT
    using reference = value_type &;                       // NOLINT
    using iterator_category = std::forward_iterator_tag;  // NOLINT

    // use reinterpret_cast because while the references are in principle
    // convertible, the compiler doesn't know that at this point in the file
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
    Iterator(const ddwaf_map_obj &map, nb_entries_t index = 0)
        : map_or_arr_{*reinterpret_cast<const ddwaf_obj *>(&map)},
          index_{index} {}

    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
    Iterator(const ddwaf_arr_obj &arr, nb_entries_t index = 0)
        : map_or_arr_{*reinterpret_cast<const ddwaf_obj *>(&arr)},
          index_{index} {}

    bool operator!=(const Iterator &other) const {
      return index_ != other.index_ || &map_or_arr_ != &other.map_or_arr_;
    }

    nb_entries_t operator-(const Iterator &other) const {
      return index_ - other.index_;
    }

    Iterator &operator++() {
      index_++;
      return *this;
    }

    value_type &operator*() const {
      return *reinterpret_cast<ddwaf_obj *>(&map_or_arr_.array[index_]);
    }

   private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const ddwaf_obj &map_or_arr_;
    nb_entries_t index_;
  };
};

// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) ddwaf_str_obj : ddwaf_obj {
  ddwaf_str_obj() : ddwaf_obj{} { type = DDWAF_OBJ_STRING; }
  explicit ddwaf_str_obj(const ddwaf_object &dobj) : ddwaf_obj(dobj) {
    if (dobj.type != DDWAF_OBJ_STRING) {
      throw std::invalid_argument("not a string");
    }
  }

  std::string_view value() const {
    return std::string_view{stringValue, nbEntries};
  }
};

struct __attribute__((__may_alias__)) ddwaf_arr_obj : ddwaf_obj {
  ddwaf_arr_obj() : ddwaf_obj{} { type = DDWAF_OBJ_ARRAY; }
  // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
  ddwaf_arr_obj(const ddwaf_object &dobj) : ddwaf_obj(dobj) {
    if (dobj.type != DDWAF_OBJ_ARRAY) {
      throw std::invalid_argument("not an array");
    }
  }

  template <typename T = ddwaf_obj>
  T &at_unchecked(nb_entries_t index) const {
    static_assert(std::is_base_of<ddwaf_obj, T>::value,
                  "T must be a subclass of ddwaf_obj");
    return *reinterpret_cast<T *>(&array[index]);  // NOLINT
  }

  template <typename T = ddwaf_object>
  T &at(nb_entries_t index) const {
    if (index >= nbEntries) {
      throw std::out_of_range("index out of range");
    }
    return at_unchecked<T>(index);
  }

  std::size_t size() const { return nbEntries; }

  bool empty() const { return nbEntries == 0; }

  Iterator begin() const { return {*this}; }
  Iterator end() const { return {*this, nbEntries}; }
};

struct ddwaf_map_obj : ddwaf_obj {
  ddwaf_map_obj() : ddwaf_obj{} { type = DDWAF_OBJ_MAP; }
  explicit ddwaf_map_obj(const ddwaf_object &dobj) : ddwaf_obj(dobj) {
    if (dobj.type != DDWAF_OBJ_MAP) {
      throw std::invalid_argument("not a map");
    }
  }

  template <typename T = ddwaf_obj>
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

  template <typename T = ddwaf_obj>
  T &get_entry_unchecked(std::size_t i) {
    static_assert(std::is_base_of_v<ddwaf_obj, T>,
                  "T must be a subclass of ddwaf_obj");
    return *reinterpret_cast<T *>(&array[i]);  // NOLINT
  }

  std::size_t size() const { return nbEntries; }

  bool empty() const { return nbEntries == 0; }

  Iterator begin() const { return {*this}; }
  Iterator end() const { return {*this, nbEntries}; }
};

template <typename T = ddwaf_obj>
class ddwaf_owned_obj {  // NOLINT(readability-identifier-naming)
  T obj_;
  DdwafMemres memres_;

 public:
  ddwaf_owned_obj() : obj_{} {}
  template <typename OT>
  explicit ddwaf_owned_obj(ddwaf_owned_obj<OT> &&oth)
      : obj_{oth.get()}, memres_{std::move(oth.memres())} {
    static_cast<ddwaf_object &>(oth.get()) =
        ddwaf_object{.type = DDWAF_OBJ_INVALID};
  }
  template <typename OT>
  ddwaf_owned_obj<T> &operator=(ddwaf_owned_obj<OT> &&oth) = delete;
  template <typename OT>
  ddwaf_owned_obj(const ddwaf_owned_obj<OT> &) = delete;
  template <typename OT>
  ddwaf_owned_obj<T> &operator=(const ddwaf_owned_obj<OT> &) = delete;

  T &get() { return obj_; }
  const T &get() const { return obj_; }
  DdwafMemres &memres() { return memres_; }
};

using ddwaf_owned_map = ddwaf_owned_obj<ddwaf_map_obj>;  // NOLINT
using ddwaf_owned_arr = ddwaf_owned_obj<ddwaf_arr_obj>;  // NOLINT

struct DdwafObjectFreeFunctor {
  void operator()(ddwaf_object &res) { ddwaf_object_free(&res); }
};
template <typename T = ddwaf_obj>
class libddwaf_ddwaf_owned_obj  // NOLINT(readability-identifier-naming)
    : public FreeableResource<T, DdwafObjectFreeFunctor> {
  using FreeableResource<T, DdwafObjectFreeFunctor>::FreeableResource;
};

ddwaf_owned_obj<ddwaf_obj> json_to_object(
    const rapidjson::GenericValue<rapidjson::UTF8<>> &doc);

// for objects created with libddwaf functions
template <typename T>
// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) libddwaf_owned_ddwaf_obj : T {
  static auto constexpr inline kInvalid =
      ddwaf_object{.type = DDWAF_OBJ_INVALID};

  libddwaf_owned_ddwaf_obj(T const &obj) : T{obj} {}
  libddwaf_owned_ddwaf_obj(const libddwaf_owned_ddwaf_obj &) = delete;
  libddwaf_owned_ddwaf_obj &operator=(const libddwaf_owned_ddwaf_obj &) =
      delete;
  libddwaf_owned_ddwaf_obj(libddwaf_owned_ddwaf_obj &&oth) noexcept
      : libddwaf_owned_ddwaf_obj{{oth}} {
    static_cast<ddwaf_object &>(*this) = kInvalid;
  };
  libddwaf_owned_ddwaf_obj &operator=(libddwaf_owned_ddwaf_obj &&oth) noexcept {
    if (this != &oth) {
      static_cast<ddwaf_object &>(*this) = *oth;
      static_cast<ddwaf_object &>(*oth) = kInvalid;
    }
    return *this;
  }

  ~libddwaf_owned_ddwaf_obj() { ddwaf_object_free(this); }
};

namespace impl {
void deep_copy(DdwafMemres &memres, ddwaf_obj &dst, const ddwaf_obj &src);
}  // namespace impl

template <typename T>
ddwaf_owned_obj<T> ddwaf_obj_clone(const T &obj) {
  ddwaf_owned_obj<T> clone;

  impl::deep_copy(clone.memres(), clone.get(), obj);

  return std::move(clone);
}
}  // namespace datadog::nginx::security
