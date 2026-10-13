#pragma once

#include <ddwaf.h>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ddwaf_memres.h"
#include "util.h"

namespace datadog::nginx::security {

struct __attribute__((__may_alias__)) ddwaf_kv_obj;
struct __attribute__((__may_alias__)) ddwaf_str_obj;
struct __attribute__((__may_alias__)) ddwaf_arr_obj;
struct __attribute__((__may_alias__)) ddwaf_map_obj;

// Wraps a ddwaf_object. ddwaf_object is a union, so we cannot inherit from it.
// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) ddwaf_obj {
  static constexpr std::size_t kMaxCompactSize =
      std::numeric_limits<uint16_t>::max();
  static constexpr std::size_t kMaxLargeSize = (std::size_t{1} << 28) - 1;

  ddwaf_object o;

  ddwaf_obj() : o{} {}
  explicit ddwaf_obj(const ddwaf_object &dobj) : o{dobj} {}

  static ddwaf_obj &from(ddwaf_object &dobj) noexcept {
    return reinterpret_cast<ddwaf_obj &>(dobj);  // NOLINT
  }
  static const ddwaf_obj &from(const ddwaf_object &dobj) noexcept {
    return reinterpret_cast<const ddwaf_obj &>(dobj);  // NOLINT
  }

  // Checked cast to a subtype (ddwaf_map_obj, etc.)
  template <typename T>
  static T &cast(ddwaf_obj &obj) {
    if (!T::has_expected_type(obj)) {
      throw std::invalid_argument("unexpected ddwaf object type");
    }
    return reinterpret_cast<T &>(obj);  // NOLINT
  }
  template <typename T>
  static const T &cast(const ddwaf_obj &obj) {
    return cast<T>(const_cast<ddwaf_obj &>(obj));  // NOLINT
  }

  // Cast to a subtype when the type was already checked.
  template <typename T>
  static const T &cast_unchecked(const ddwaf_obj &obj) noexcept {
    assert(T::has_expected_type(obj));
    return reinterpret_cast<const T &>(obj);  // NOLINT
  }

  static bool has_expected_type(const ddwaf_obj &) noexcept { return true; }

  ddwaf_object &raw() noexcept { return o; }
  const ddwaf_object &raw() const noexcept { return o; }

  // For SMALL_STRING, the view points into this object.
  std::string_view string_val_unchecked() const noexcept {
    if (o.type == DDWAF_OBJ_SMALL_STRING) {
      return {o.via.sstr.data, o.via.sstr.size};
    }
    if (o.type == DDWAF_OBJ_STRING || o.type == DDWAF_OBJ_LITERAL_STRING) {
      if (o.via.str.ptr == nullptr) {
        return {};
      }
      return {o.via.str.ptr, o.via.str.size};
    }
    return {};
  }

  bool is_numeric() const noexcept {
    return o.type == DDWAF_OBJ_SIGNED || o.type == DDWAF_OBJ_UNSIGNED ||
           o.type == DDWAF_OBJ_FLOAT;
  }

  bool is_bool() const noexcept { return o.type == DDWAF_OBJ_BOOL; }

  bool is_null() const noexcept { return o.type == DDWAF_OBJ_NULL; }

  bool is_string() const noexcept {
    return o.type == DDWAF_OBJ_STRING || o.type == DDWAF_OBJ_LITERAL_STRING ||
           o.type == DDWAF_OBJ_SMALL_STRING;
  }

  bool is_map() const noexcept {
    return o.type == DDWAF_OBJ_MAP || o.type == DDWAF_OBJ_LARGE_MAP;
  }

  bool is_array() const noexcept {
    return o.type == DDWAF_OBJ_ARRAY || o.type == DDWAF_OBJ_LARGE_ARRAY;
  }

  bool is_large() const noexcept {
    return o.type == DDWAF_OBJ_LARGE_ARRAY || o.type == DDWAF_OBJ_LARGE_MAP;
  }

  // Number of elements of an array or map
  std::size_t size_unchecked() const noexcept {
    if (is_large()) {
      return o.via.large_array.size;
    }
    return o.via.array.size;
  }

  std::size_t capacity_unchecked() const noexcept {
    if (is_large()) {
      return o.via.large_array.capacity;
    }
    return o.via.array.capacity;
  }

  // Lowers or raises the size of an array or map. Keeps the capacity.
  void set_size_unchecked(std::size_t size) noexcept {
    assert(size <= capacity_unchecked());
    if (is_large()) {
      o.via.large_array.size = size;
    } else {
      o.via.array.size = static_cast<uint16_t>(size);
    }
  }

  template <typename T>
  T numeric_val() const {
    static constexpr auto min = std::numeric_limits<T>::lowest();
    static constexpr auto max = std::numeric_limits<T>::max();
    if (o.type == DDWAF_OBJ_SIGNED) {
      int64_t const value = o.via.i64.val;
      if constexpr (std::is_unsigned_v<T>) {
        // special branch to avoid implicit signed -> unsigned conversions
        if (value < 0 || static_cast<uint64_t>(value) > max) {
          throw std::out_of_range("value out of range");
        }
      } else {
        if (value < min || value > max) {
          throw std::out_of_range("value out of range");
        }
      }
      return static_cast<T>(value);
    }

    if (o.type == DDWAF_OBJ_UNSIGNED) {
      if (o.via.u64.val > max) {
        throw std::out_of_range("value out of range");
      }
      return static_cast<T>(o.via.u64.val);
    }

    if (o.type == DDWAF_OBJ_FLOAT) {
      double adjusted_max;
      if constexpr (std::is_same_v<T, int64_t>) {
        // this is the largest representable int64_t as a double
        // max=static_cast<double>(max) would be 9223372036854775808.0, which
        // is larger than the largest representable int64_t (2^63-1)
        adjusted_max =
            9223372036854774784.0;  // nextafter((double)max, -DBL_MAX)
      } else {
        adjusted_max = static_cast<double>(max);
      }
      double const value = o.via.f64.val;
      if (value < min || value > adjusted_max) {
        throw std::out_of_range("value out of range");
      }
      return static_cast<T>(value);
    }

    throw std::invalid_argument("not a numeric value");
  }

  ddwaf_obj &make_invalid() {
    o = ddwaf_object{};
    return *this;
  }

  ddwaf_obj &make_bool(bool value) {
    o.via.b8 = {.type = DDWAF_OBJ_BOOL, .val = value};
    return *this;
  }

  template <typename T>
  ddwaf_obj &make_number(T value) {
    static_assert(std::is_arithmetic_v<T>, "T must be an arithmetic type");
    if constexpr (std::is_floating_point_v<T>) {
      o.via.f64 = {.type = DDWAF_OBJ_FLOAT, .val = value};
    } else if constexpr (std::is_signed_v<T>) {
      o.via.i64 = {.type = DDWAF_OBJ_SIGNED, .val = value};
    } else {
      o.via.u64 = {.type = DDWAF_OBJ_UNSIGNED, .val = value};
    }
    return *this;
  }

  ddwaf_obj &make_null() {
    o = ddwaf_object{};
    o.type = DDWAF_OBJ_NULL;
    return *this;
  }

  // We only build DDWAF_OBJ_STRING, never SMALL_STRING.
  // The string must outlive the object.
  ddwaf_str_obj &make_string(std::string_view sv) {
    if (sv.size() > std::numeric_limits<uint32_t>::max()) {
      throw std::length_error("string too long for ddwaf object");
    }
    o.via.str = {.type = DDWAF_OBJ_STRING,
                 .size = static_cast<uint32_t>(sv.size()),
                 .ptr = const_cast<char *>(sv.data())};  // NOLINT
    return *reinterpret_cast<ddwaf_str_obj *>(this);     // NOLINT
  }

  ddwaf_str_obj &make_string(std::string_view sv, DdwafMemres &memres) {
    char *s = memres.allocate_string(sv.size());
    std::memcpy(s, sv.data(), sv.size());
    return make_string({s, sv.size()});
  }

  template <typename C>
  ddwaf_str_obj &make_string(C *) = delete;

  ddwaf_arr_obj &make_array(ddwaf_obj *arr, std::size_t size) {
    set_container(DDWAF_OBJ_ARRAY, arr, size);
    return *reinterpret_cast<ddwaf_arr_obj *>(this);  // NOLINT
  }
  ddwaf_arr_obj &make_array(std::size_t size, DdwafMemres &memres) {
    return make_array(memres.allocate_objects<ddwaf_obj>(size), size);
  }

  ddwaf_map_obj &make_map(ddwaf_kv_obj *entries, std::size_t size) {
    set_container(DDWAF_OBJ_MAP, entries, size);
    return *reinterpret_cast<ddwaf_map_obj *>(this);  // NOLINT
  }
  inline ddwaf_map_obj &make_map(std::size_t size, DdwafMemres &memres);

 protected:
  ddwaf_obj *array_ptr() const noexcept {
    return reinterpret_cast<ddwaf_obj *>(o.via.array.ptr);  // NOLINT
  }

 private:
  // Uses the large type when the size does not fit in 16 bits.
  void set_container(DDWAF_OBJ_TYPE type, void *ptr, std::size_t size) {
    if (size <= kMaxCompactSize) {
      auto const compact_size = static_cast<uint16_t>(size);
      o.via.array = {.type = static_cast<uint8_t>(type),
                     .size = compact_size,
                     .capacity = compact_size,
                     .ptr = static_cast<ddwaf_object *>(ptr)};
      return;
    }
    if (size > kMaxLargeSize) {
      throw std::length_error("container too large for ddwaf object");
    }
    o = ddwaf_object{};
    o.via.large_array._type =
        type == DDWAF_OBJ_ARRAY ? DDWAF_OBJ_LARGE_ARRAY : DDWAF_OBJ_LARGE_MAP;
    o.via.large_array.size = size;
    o.via.large_array.capacity = size;
    o.via.large_array.ptr = static_cast<ddwaf_object *>(ptr);
  }
};

// Matches ddwaf_object_kv
// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) ddwaf_kv_obj {
  ddwaf_obj key_obj;
  ddwaf_obj val;

  std::string_view key() const noexcept {
    return key_obj.string_val_unchecked();
  }

  // string lifetime must be at least that of the object
  ddwaf_kv_obj &set_key(std::string_view sv) {
    key_obj.make_string(sv);
    return *this;
  }

  ddwaf_kv_obj &set_key(std::string_view sv, DdwafMemres &memres) {
    key_obj.make_string(sv, memres);
    return *this;
  }
};

static_assert(sizeof(ddwaf_obj) == sizeof(ddwaf_object));
static_assert(sizeof(ddwaf_obj) == 16);
static_assert(alignof(ddwaf_obj) == alignof(ddwaf_object));
static_assert(std::is_standard_layout_v<ddwaf_obj>);
static_assert(sizeof(ddwaf_kv_obj) == sizeof(ddwaf_object_kv));
static_assert(sizeof(ddwaf_kv_obj) == 2 * sizeof(ddwaf_object));
static_assert(offsetof(ddwaf_kv_obj, val) == sizeof(ddwaf_object));
static_assert(alignof(ddwaf_object_kv) == alignof(ddwaf_object));
static_assert(std::is_standard_layout_v<ddwaf_kv_obj>);

// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) ddwaf_str_obj : ddwaf_obj {
  ddwaf_str_obj() : ddwaf_obj{} { make_string(std::string_view{}); }
  explicit ddwaf_str_obj(const ddwaf_obj &dobj) : ddwaf_obj(dobj) {
    if (!has_expected_type(dobj)) {
      throw std::invalid_argument("not a string");
    }
  }
  ddwaf_str_obj(const ddwaf_map_obj &) = delete;
  ddwaf_str_obj(const ddwaf_arr_obj &) = delete;

  static bool has_expected_type(const ddwaf_obj &obj) noexcept {
    return obj.is_string();
  }

  std::string_view value() const { return string_val_unchecked(); }
  char *buffer() {
    assert(o.type == DDWAF_OBJ_STRING);
    return o.via.str.ptr;
  }
};

// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) ddwaf_bool_obj : ddwaf_obj {
  ddwaf_bool_obj() : ddwaf_obj{} { make_bool(false); }
  explicit ddwaf_bool_obj(const ddwaf_obj &dobj) : ddwaf_obj(dobj) {
    if (!has_expected_type(dobj)) {
      throw std::invalid_argument("not a boolean");
    }
  }

  static bool has_expected_type(const ddwaf_obj &obj) noexcept {
    return obj.is_bool();
  }

  operator bool() const { return o.via.b8.val; }
};

// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) ddwaf_arr_obj : ddwaf_obj {
  ddwaf_arr_obj() : ddwaf_obj{} { make_array(nullptr, 0); }
  // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
  ddwaf_arr_obj(const ddwaf_obj &dobj) : ddwaf_obj(dobj) {
    if (!has_expected_type(dobj)) {
      throw std::invalid_argument("not an array");
    }
  }
  ddwaf_arr_obj(const ddwaf_str_obj &) = delete;
  ddwaf_arr_obj(const ddwaf_map_obj &) = delete;

  static bool has_expected_type(const ddwaf_obj &obj) noexcept {
    return obj.is_array();
  }

  template <typename T = ddwaf_obj>
  T &at_unchecked(std::size_t index) const {
    static_assert(std::is_base_of_v<ddwaf_obj, T>,
                  "T must be a subclass of ddwaf_obj");
    return *reinterpret_cast<T *>(&array_ptr()[index]);  // NOLINT
  }

  template <typename T = ddwaf_obj>
  T &at(std::size_t index) const {
    if (index >= size()) {
      throw std::out_of_range("index out of range");
    }
    return at_unchecked<T>(index);
  }

  std::size_t size() const { return size_unchecked(); }
  void set_size(std::size_t size) { set_size_unchecked(size); }

  bool empty() const { return size() == 0; }

  ddwaf_obj *begin() const { return array_ptr(); }
  ddwaf_obj *end() const { return array_ptr() + size(); }
};

// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) ddwaf_map_obj : ddwaf_obj {
  ddwaf_map_obj() : ddwaf_obj{} { make_map(nullptr, 0); }
  explicit ddwaf_map_obj(const ddwaf_obj &dobj) : ddwaf_obj(dobj) {
    if (!has_expected_type(dobj)) {
      throw std::invalid_argument("not a map");
    }
  }
  ddwaf_map_obj(const ddwaf_str_obj &) = delete;
  ddwaf_map_obj(const ddwaf_arr_obj &) = delete;

  static bool has_expected_type(const ddwaf_obj &obj) noexcept {
    return obj.is_map();
  }

  // Returns the stored value by reference; throws if the key is missing.
  template <typename T = ddwaf_obj>
  T &get(std::string_view key) {
    T *value = get_opt<T>(key);
    if (value == nullptr) {
      throw std::out_of_range(std::string{"key "} + std::string{key} +
                              " not found");
    }
    return *value;
  }
  template <typename T = ddwaf_obj>
  const T &get(std::string_view key) const {
    return const_cast<ddwaf_map_obj *>(this)->get<T>(key);  // NOLINT
  }

  // Returns a pointer to the stored value, or nullptr if the key is missing.
  // A small string can be copied, but a view into a temporary copy dangles
  // when that copy is destroyed.
  // Throws if the type is wrong.
  template <typename T = ddwaf_obj>
  T *get_opt(std::string_view key) {
    static_assert(std::is_base_of_v<ddwaf_obj, T>,
                  "T must be a subclass of ddwaf_obj");
    for (ddwaf_kv_obj &entry : *this) {
      if (entry.key() == key) {
        return &cast<T>(entry.val);
      }
    }
    return nullptr;
  }
  template <typename T = ddwaf_obj>
  const T *get_opt(std::string_view key) const {
    return const_cast<ddwaf_map_obj *>(this)->get_opt<T>(key);  // NOLINT
  }

  ddwaf_kv_obj &at_unchecked(std::size_t index) const { return begin()[index]; }

  std::size_t size() const { return size_unchecked(); }
  void set_size(std::size_t size) { set_size_unchecked(size); }

  bool empty() const { return size() == 0; }

  ddwaf_kv_obj *begin() const {
    return reinterpret_cast<ddwaf_kv_obj *>(array_ptr());  // NOLINT
  }
  ddwaf_kv_obj *end() const { return begin() + size(); }
};

inline ddwaf_map_obj &ddwaf_obj::make_map(std::size_t size,
                                          DdwafMemres &memres) {
  return make_map(memres.allocate_kvs<ddwaf_kv_obj>(size), size);
}

template <typename T = ddwaf_obj>
class ddwaf_owned_obj {  // NOLINT(readability-identifier-naming)
  T obj_;
  DdwafMemres memres_;

 public:
  ddwaf_owned_obj() : obj_{} {}
  template <typename OT>
  explicit ddwaf_owned_obj(ddwaf_owned_obj<OT> &&oth)
      : obj_{oth.get()}, memres_{std::move(oth.memres())} {
    oth.get().o = ddwaf_object{};
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

// For objects allocated by libddwaf with the default allocator
struct DdwafObjectFreeFunctor {
  void operator()(ddwaf_obj &res) {
    ddwaf_object_destroy(&res.o, ddwaf_get_default_allocator());
  }
};
template <typename T = ddwaf_obj>
class libddwaf_ddwaf_owned_obj  // NOLINT(readability-identifier-naming)
    : public FreeableResource<T, DdwafObjectFreeFunctor> {
  using FreeableResource<T, DdwafObjectFreeFunctor>::FreeableResource;
};

ddwaf_owned_obj<ddwaf_obj> json_to_object(
    const rapidjson::GenericValue<rapidjson::UTF8<>> &doc, int max_depth);

// for objects created by libddwaf with the default allocator
template <typename T>
// NOLINTNEXTLINE(readability-identifier-naming)
struct __attribute__((__may_alias__)) libddwaf_owned_ddwaf_obj : T {
  static constexpr ddwaf_object kInvalid{};

  libddwaf_owned_ddwaf_obj() : T{} { this->o = kInvalid; }
  explicit libddwaf_owned_ddwaf_obj(T const &obj) : T{obj} {}
  libddwaf_owned_ddwaf_obj(const libddwaf_owned_ddwaf_obj &) = delete;
  libddwaf_owned_ddwaf_obj &operator=(const libddwaf_owned_ddwaf_obj &) =
      delete;
  libddwaf_owned_ddwaf_obj(libddwaf_owned_ddwaf_obj &&oth) noexcept : T{} {
    this->o = oth.o;
    oth.o = kInvalid;
  };
  libddwaf_owned_ddwaf_obj &operator=(libddwaf_owned_ddwaf_obj &&oth) noexcept {
    if (this != &oth) {
      ddwaf_object_destroy(&this->o, ddwaf_get_default_allocator());
      this->o = oth.o;
      oth.o = kInvalid;
    }
    return *this;
  }

  ~libddwaf_owned_ddwaf_obj() {
    ddwaf_object_destroy(&this->o, ddwaf_get_default_allocator());
  }
};

namespace impl {
void deep_copy(DdwafMemres &memres, ddwaf_obj &dst, const ddwaf_obj &src);
}  // namespace impl

template <typename T>
ddwaf_owned_obj<T> ddwaf_obj_clone(const T &obj) {
  ddwaf_owned_obj<T> clone;

  impl::deep_copy(clone.memres(), clone.get(), obj);

  return clone;
}
}  // namespace datadog::nginx::security
