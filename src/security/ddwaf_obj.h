#pragma once

#include <ddwaf.h>
#include <rapidjson/rapidjson.h>

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
  ddwaf_obj &set_key(std::string_view sv) noexcept {
    parameterName = sv.data();
    parameterNameLength = sv.length();
    return *this;
  }

  ddwaf_obj &set_key(std::string_view sv, ddwaf_memres &memres) {
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

  template<typename T>
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
    stringValue = const_cast<char *>(sv.data()); // NOLINT
    nbEntries = sv.length();
    return *reinterpret_cast<ddwaf_str_obj *>(this); // NOLINT
  }

  ddwaf_str_obj &make_string(std::string_view sv, ddwaf_memres &memres) {
    char *s = memres.allocate_string(sv.size());
    std::memcpy(s, sv.data(), sv.size());
    return make_string({s, sv.size()});
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

  template<typename T>
  T& shallow_copy_val_from(const T& oth) {
    static_assert(std::is_base_of<ddwaf_obj, T>::value,
                  "T must be a subclass of ddwaf_obj");

    // does not copy the key
    std::memcpy(
      reinterpret_cast<char*>(this) + offsetof(ddwaf_obj, stringValue),
      reinterpret_cast<const char*>(&oth) + offsetof(ddwaf_obj, stringValue),
      sizeof (ddwaf_obj) - offsetof(ddwaf_obj, stringValue)
    );
    return reinterpret_cast<T&>(*this); // NOLINT
  }
};

struct may_alias ddwaf_str_obj : ddwaf_obj {
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

struct may_alias ddwaf_arr_obj : ddwaf_obj {
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
    return *reinterpret_cast<T*>(&array[index]); // NOLINT
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

  template<typename T = ddwaf_obj>
  T& get_entry_unchecked(std::size_t i) {
    static_assert(std::is_base_of_v<ddwaf_obj, T>,
                  "T must be a subclass of ddwaf_obj");
    return *reinterpret_cast<T*>(&array[i]); // NOLINT
  }

  std::size_t size() const {
    return nbEntries;
  }

  bool empty() const {
    return nbEntries == 0;
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
};

template<typename T = ddwaf_obj>
class ddwaf_owned_obj {
  ddwaf_memres memres_;
  T obj_;
 public:
  ddwaf_owned_obj() = default;
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
  ddwaf_memres& memres() { return memres_; }
};

using ddwaf_owned_map = ddwaf_owned_obj<ddwaf_map_obj>;
using ddwaf_owned_arr = ddwaf_owned_obj<ddwaf_arr_obj>;

namespace {
template <typename D>
void json_to_obj_impl(ddwaf_memres &memres, ddwaf_obj &object, D &doc) {
  switch (doc.GetType()) {
    case rapidjson::kFalseType:
      object.make_bool(false);
      break;
    case rapidjson::kTrueType:
      object.make_bool(true);
      break;
    case rapidjson::kObjectType: {
      auto &&obj = doc.GetObject();
      ddwaf_map_obj &obj_map = object.make_map(obj.MemberCount(), memres);
      size_t i = 0;
      for (auto &kv : obj) {
        std::string_view const key = kv.name.GetString();
        ddwaf_obj &element = obj_map.get_entry_unchecked(i++);
        element.set_key(key, memres);
        json_to_object_impl(memres, element, kv.value);
      }
      break;
    }
    case rapidjson::kArrayType: {
      auto &&arr = doc.getArray();
      ddwaf_arr_obj &obj_arr = object.make_array(arr.Size(), memres);
      size_t i = 0;
      for (auto &v : arr) {
        ddwaf_obj &element = obj_arr.at_unchecked(i++);
        json_to_object_impl(memres, element, v);
      }
      break;
    }
    case rapidjson::kStringType: {
      std::string_view sv{doc.GetString(), doc.GetStringLength()};
      object.make_string(sv, memres);
      break;
    }
    case rapidjson::kNumberType: {
      if (doc.IsInt64()) {
        object.make_number(doc.GetInt64());
      } else if (doc.IsUint64()) {
        object.make_number(doc.GetUint64());
      }
      break;
    }
    case rapidjson::kNullType:
    default:
      object.make_null();
      break;
  }
}
}

template <typename D>
ddwaf_owned_obj<ddwaf_obj> json_to_object(D &doc) { // NOLINT(misc-no-recursion)
  ddwaf_owned_obj<ddwaf_obj> ret;
  json_to_obj_impl(ret.memres(), ret.get(), doc);
}

// for objects created with libddwaf functions
template<typename T>
struct may_alias libddwaf_owned_ddwaf_obj : T {
  static auto constexpr inline invalid =
      ddwaf_object{.type = DDWAF_OBJ_INVALID};

  libddwaf_owned_ddwaf_obj(T const &obj) : T{obj} {}
  libddwaf_owned_ddwaf_obj(const libddwaf_owned_ddwaf_obj&) = delete;
  libddwaf_owned_ddwaf_obj& operator=(const libddwaf_owned_ddwaf_obj&) = delete;
  libddwaf_owned_ddwaf_obj(libddwaf_owned_ddwaf_obj&& oth) noexcept : libddwaf_owned_ddwaf_obj{{oth}} {
    static_cast<ddwaf_object&>(*this) = invalid;
  };
  libddwaf_owned_ddwaf_obj &operator=(libddwaf_owned_ddwaf_obj &&oth) noexcept {
    if (this != &oth) {
      static_cast<ddwaf_object &>(*this) = *oth;
      static_cast<ddwaf_object &>(*oth) = invalid;
    }
    return *this;
  }

  ~libddwaf_owned_ddwaf_obj() {
    ddwaf_object_free(this);
  }
};

namespace {
  inline void copy(ddwaf_memres &memres, ddwaf_obj &dst, ddwaf_obj &src) {
    switch(src.type) {
      case DDWAF_OBJ_MAP: {
        ddwaf_map_obj src_map{src};
        ddwaf_map_obj &r = dst.make_map(src.nbEntries, memres);
        size_t i = 0;
        for (auto &&[k, v]: src_map) {
          ddwaf_obj &new_dst = r.get_entry_unchecked(i++);
          new_dst.set_key(k, memres);
          copy(memres, new_dst, v);
        }
        break;
      }
      case DDWAF_OBJ_ARRAY: {
        ddwaf_arr_obj src_arr{src};
        ddwaf_arr_obj &r = dst.make_array(src.nbEntries, memres);
        size_t i = 0;
        for (auto &&elem: src_arr) {
          ddwaf_obj &new_dst = r.at_unchecked(i++);
          copy(memres, new_dst, elem);
        }
        break;
      }
      case DDWAF_OBJ_STRING: {
        dst.make_string(src.string_val_unchecked(), memres);
        break;
      }
      default:
        dst.shallow_copy_val_from(src);
  }
}  // namespace

template<typename T>
ddwaf_owned_obj<T> ddwaf_obj_clone(const T obj) {
  ddwaf_owned_obj<T> clone;

  copy(clone.memres(), clone.get(), obj);

  return std::move(clone);
}

}  // namespace datadog::nginx::security
}

