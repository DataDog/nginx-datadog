#pragma once

#include <ddwaf.h>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include <cassert>
#include <concepts>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "ddwaf_memres.h"

namespace datadog::nginx::security {

// ddwaf_obj is a union of typed views of one ddwaf_object: any, str, b, arr
// and map. They all start with the same ddwaf_object, so it can be read
// through any of them (common initial sequence).
//
// ddwaf_object is itself a union of `type` and `via`. Reading `type` after
// writing a `via` member is not covered by that rule. It relies on the union
// type punning that GCC and Clang allow, as libddwaf's C API does.
//
// Rule: the active member always matches the type tag. make_*() create the
// matching member. Code that writes the ddwaf_object in other ways (e.g.
// libddwaf output) must call sync() afterwards.
//
// This file declares the types in the order C++ needs them. The longer
// function bodies come at the end, from higher to lower level.
union ddwaf_obj;
struct ddwaf_kv_obj;
struct ddwaf_str_obj;
struct ddwaf_bool_obj;
struct ddwaf_arr_obj;
struct ddwaf_map_obj;

// Checked cast to a member type (ddwaf_map_obj, etc.), or to ddwaf_obj
template <typename T>
T &cast(ddwaf_obj &obj);
template <typename T>
const T &cast(const ddwaf_obj &obj);
// Cast when the type was already checked
template <typename T>
const T &cast_unchecked(const ddwaf_obj &obj) noexcept;

// cast(obj) without a type: becomes a reference to the member type it is
// bound to, with the same check as cast<T>(obj).
// NOLINTNEXTLINE(readability-identifier-naming)
class ddwaf_obj_cast {
 public:
  explicit ddwaf_obj_cast(ddwaf_obj &obj) noexcept : obj_{obj} {}
  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
  operator T &() const {
    return cast<std::remove_const_t<T>>(obj_);
  }

 private:
  ddwaf_obj &obj_;
};

// NOLINTNEXTLINE(readability-identifier-naming)
class ddwaf_obj_const_cast {
 public:
  explicit ddwaf_obj_const_cast(const ddwaf_obj &obj) noexcept : obj_{obj} {}
  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
  operator const T &() const {
    return cast<T>(obj_);
  }

 private:
  const ddwaf_obj &obj_;
};

inline ddwaf_obj_cast cast(ddwaf_obj &obj) noexcept {
  return ddwaf_obj_cast{obj};
}
inline ddwaf_obj_const_cast cast(const ddwaf_obj &obj) noexcept {
  return ddwaf_obj_const_cast{obj};
}

// Reads a ddwaf_object. Shared by ddwaf_obj and its members. Reading
// `type` relies on union type punning (see the top of this file).
namespace obj_read {
inline bool is_numeric(const ddwaf_object &o) noexcept;
inline bool is_bool(const ddwaf_object &o) noexcept;
inline bool is_null(const ddwaf_object &o) noexcept;
inline bool is_string(const ddwaf_object &o) noexcept;
inline bool is_map(const ddwaf_object &o) noexcept;
inline bool is_array(const ddwaf_object &o) noexcept;
// Number of elements of an array or map
inline std::size_t size_unchecked(const ddwaf_object &o) noexcept;
inline std::size_t capacity_unchecked(const ddwaf_object &o) noexcept;
// For SMALL_STRING, the view points into the object.
inline std::string_view string_val(const ddwaf_object &o) noexcept;
template <typename T>
T numeric_val(const ddwaf_object &o);
}  // namespace obj_read

// What all members of ddwaf_obj have: the data, and the helpers they share.
// NOLINTNEXTLINE(readability-identifier-naming)
struct ddwaf_obj_base {
  ddwaf_object o;

 protected:
  friend union ddwaf_obj;

  // Uses the large type when the size does not fit in 16 bits.
  void set_array(ddwaf_obj *elems, std::size_t size);
  void set_map(ddwaf_kv_obj *entries, std::size_t size);

  // Lowers or raises the size of an array or map. Keeps the capacity.
  void set_size_unchecked(std::size_t size) noexcept;

  // The elements of an array
  ddwaf_obj *array_ptr_unchecked() const noexcept;
  // The entries of a map
  ddwaf_kv_obj *map_ptr_unchecked() const noexcept;

 private:
  static constexpr std::size_t kMaxCompactSize =
      std::numeric_limits<uint16_t>::max();
  static constexpr std::size_t kMaxLargeSize = (std::size_t{1} << 28) - 1;

  static void check_large_size(std::size_t size);
};

// The typed members. Only ddwaf_obj can create or copy them, so they never
// exist outside the union.

// NOLINTNEXTLINE(readability-identifier-naming)
struct ddwaf_str_obj : ddwaf_obj_base {
  static bool has_expected_type(const ddwaf_object &o) noexcept {
    return obj_read::is_string(o);
  }

  std::string_view value() const noexcept { return obj_read::string_val(o); }

 private:
  friend union ddwaf_obj;
  ddwaf_str_obj() = default;
  explicit ddwaf_str_obj(const ddwaf_object &src) : ddwaf_obj_base{src} {}
  ddwaf_str_obj(const ddwaf_str_obj &) = default;
  ddwaf_str_obj &operator=(const ddwaf_str_obj &) = default;
};

// NOLINTNEXTLINE(readability-identifier-naming)
struct ddwaf_bool_obj : ddwaf_obj_base {
  static bool has_expected_type(const ddwaf_object &o) noexcept {
    return obj_read::is_bool(o);
  }

  // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
  operator bool() const { return o.via.b8.val; }

 private:
  friend union ddwaf_obj;
  ddwaf_bool_obj() = default;
  explicit ddwaf_bool_obj(const ddwaf_object &src) : ddwaf_obj_base{src} {}
  ddwaf_bool_obj(const ddwaf_bool_obj &) = default;
  ddwaf_bool_obj &operator=(const ddwaf_bool_obj &) = default;
};

// NOLINTNEXTLINE(readability-identifier-naming)
struct ddwaf_arr_obj : ddwaf_obj_base {
  static bool has_expected_type(const ddwaf_object &o) noexcept {
    return obj_read::is_array(o);
  }

  template <typename T = ddwaf_obj>
  T &at(std::size_t index) const;

  template <typename T = ddwaf_obj>
  T &at_unchecked(std::size_t index) const;

  std::size_t size() const noexcept { return obj_read::size_unchecked(o); }

  bool empty() const noexcept { return size() == 0; }

  // Grows the array by one, within its capacity, and returns the new slot.
  // Throws if the array is full.
  ddwaf_obj &append();

  // Keeps the capacity, so append() can fill the array again.
  void clear() noexcept { set_size_unchecked(0); }

  ddwaf_obj *begin() const noexcept { return array_ptr_unchecked(); }
  ddwaf_obj *end() const noexcept;

 private:
  friend union ddwaf_obj;
  ddwaf_arr_obj() = default;
  explicit ddwaf_arr_obj(const ddwaf_object &src) : ddwaf_obj_base{src} {}
  ddwaf_arr_obj(const ddwaf_arr_obj &) = default;
  ddwaf_arr_obj &operator=(const ddwaf_arr_obj &) = default;
};

// NOLINTNEXTLINE(readability-identifier-naming)
struct ddwaf_map_obj : ddwaf_obj_base {
  static bool has_expected_type(const ddwaf_object &o) noexcept {
    return obj_read::is_map(o);
  }

  // Returns the stored value by reference; throws if the key is missing.
  template <typename T = ddwaf_obj>
  T &get(std::string_view key);
  template <typename T = ddwaf_obj>
  const T &get(std::string_view key) const {
    return const_cast<ddwaf_map_obj *>(this)->get<T>(key);  // NOLINT
  }

  // Returns a pointer to the stored value, or nullptr if the key is missing.
  // A small string can be copied, but a view into a temporary copy dangles
  // when that copy is destroyed.
  // Throws if the type is wrong.
  template <typename T = ddwaf_obj>
  T *get_opt(std::string_view key);
  template <typename T = ddwaf_obj>
  const T *get_opt(std::string_view key) const {
    return const_cast<ddwaf_map_obj *>(this)->get_opt<T>(key);  // NOLINT
  }

  ddwaf_kv_obj &at_unchecked(std::size_t index) const;

  std::size_t size() const noexcept { return obj_read::size_unchecked(o); }

  bool empty() const noexcept { return size() == 0; }

  // Lowers or raises the size. Keeps the capacity.
  using ddwaf_obj_base::set_size_unchecked;

  ddwaf_kv_obj *begin() const noexcept { return map_ptr_unchecked(); }
  ddwaf_kv_obj *end() const noexcept;

 private:
  friend union ddwaf_obj;
  ddwaf_map_obj() = default;
  explicit ddwaf_map_obj(const ddwaf_object &src) : ddwaf_obj_base{src} {}
  ddwaf_map_obj(const ddwaf_map_obj &) = default;
  ddwaf_map_obj &operator=(const ddwaf_map_obj &) = default;
};

// NOLINTNEXTLINE(readability-identifier-naming)
union ddwaf_obj {
  // Active for the types DDWAF_OBJ_INVALID and DDWAF_OBJ_NULL, and for
  // numbers. ddwaf_obj() makes it active. It is first so that
  // zero-initialization (of objects with static storage) also starts it.
  ddwaf_obj_base any;
  ddwaf_str_obj str;
  ddwaf_bool_obj b;
  ddwaf_arr_obj arr;
  ddwaf_map_obj map;

  ddwaf_obj() : any{} {}

  // The data, for writing: the o of the member that matches the type tag,
  // which by the rule is the active one. A write through a reference does
  // not make a union member active, so this cannot be any.o. After changing
  // the type tag through it, do not call raw() again until sync(): the tag
  // no longer names the active member.
  ddwaf_object &raw() noexcept { return active().o; }
  // The data, for reading. Reading any.o is allowed whatever member is
  // active: all members start with o (their common initial sequence).
  const ddwaf_object &raw() const noexcept { return any.o; }

  // Makes the member that matches the type tag the active one. Call after
  // writing the ddwaf_object other than through make_*().
  void sync() noexcept;

  std::string_view string_val() const noexcept {
    return obj_read::string_val(raw());
  }
  bool is_numeric() const noexcept { return obj_read::is_numeric(raw()); }
  bool is_bool() const noexcept { return obj_read::is_bool(raw()); }
  bool is_null() const noexcept { return obj_read::is_null(raw()); }
  bool is_string() const noexcept { return obj_read::is_string(raw()); }
  bool is_map() const noexcept { return obj_read::is_map(raw()); }
  bool is_array() const noexcept { return obj_read::is_array(raw()); }
  std::size_t size_unchecked() const noexcept {
    return obj_read::size_unchecked(raw());
  }
  template <typename T>
  T numeric_val() const {
    return obj_read::numeric_val<T>(raw());
  }

  ddwaf_bool_obj &make_bool(bool value);
  template <typename T>
  ddwaf_obj &make_number(T value);
  ddwaf_obj &make_null();
  ddwaf_obj &make_invalid();

  // We only build DDWAF_OBJ_STRING, never SMALL_STRING.
  // The string must outlive the object.
  ddwaf_str_obj &make_string(std::string_view sv);
  ddwaf_str_obj &make_string(std::string_view sv, DdwafMemres &memres);
  template <typename C>
  ddwaf_str_obj &make_string(C *) = delete;

  ddwaf_arr_obj &make_array(ddwaf_obj *elems, std::size_t size);
  ddwaf_arr_obj &make_array(std::size_t size, DdwafMemres &memres);

  ddwaf_map_obj &make_map(ddwaf_kv_obj *entries, std::size_t size);
  ddwaf_map_obj &make_map(std::size_t size, DdwafMemres &memres);

 private:
  template <typename T>
  friend T &cast(ddwaf_obj &obj);
  template <typename T>
  friend const T &cast(const ddwaf_obj &obj);
  template <typename T>
  friend const T &cast_unchecked(const ddwaf_obj &obj) noexcept;

  // The member that matches the type tag: by the rule, the active one
  ddwaf_obj_base &active() noexcept;

  template <typename T>
  T &member_unchecked() noexcept;
};

// Matches ddwaf_object_kv
// NOLINTNEXTLINE(readability-identifier-naming)
struct ddwaf_kv_obj {
  ddwaf_obj key_obj;
  ddwaf_obj val;

  std::string_view key() const noexcept { return key_obj.string_val(); }

  // string lifetime must be at least that of the object
  ddwaf_kv_obj &set_key(std::string_view sv);
  ddwaf_kv_obj &set_key(std::string_view sv, DdwafMemres &memres);
};

// The layouts that libddwaf relies on when it reads our containers, and
// that we rely on when we read its containers (see set_array() and
// set_map()).
static_assert(sizeof(ddwaf_obj) == sizeof(ddwaf_object));
static_assert(sizeof(ddwaf_obj) == 16);
static_assert(alignof(ddwaf_obj) == alignof(ddwaf_object));
static_assert(std::is_standard_layout_v<ddwaf_obj>);
// copied with memcpy and memmove, and created implicitly by them
static_assert(std::is_trivially_copyable_v<ddwaf_obj>);
static_assert(sizeof(ddwaf_kv_obj) == sizeof(ddwaf_object_kv));
static_assert(sizeof(ddwaf_kv_obj) == 2 * sizeof(ddwaf_object));
static_assert(offsetof(ddwaf_kv_obj, val) == sizeof(ddwaf_object));
static_assert(alignof(ddwaf_object_kv) == alignof(ddwaf_object));
static_assert(alignof(ddwaf_kv_obj) == alignof(ddwaf_object));
static_assert(std::is_standard_layout_v<ddwaf_kv_obj>);
static_assert(std::is_trivially_destructible_v<ddwaf_kv_obj>);

// What the union design relies on, besides the union itself (a union and its
// members are always pointer-interconvertible):
// - ddwaf_obj* and ddwaf_object* convert to each other (set_array(),
//   array_ptr_unchecked()): each member is pointer-interconvertible with its
//   ddwaf_obj_base, which is pointer-interconvertible with its o.
// - any.o can be read whatever member is active: o is in the common initial
//   sequence of ddwaf_obj_base and each member.
// libc++ has neither std::is_pointer_interconvertible_with_class nor
// std::is_corresponding_member, so with it we use clang builtins, or check
// the definition directly.
#ifdef __cpp_lib_is_pointer_interconvertible
static_assert(std::is_pointer_interconvertible_with_class(&ddwaf_obj_base::o));
#else
// the definition, for a standard-layout class with a single member
static_assert(std::is_standard_layout_v<ddwaf_obj_base> &&
              offsetof(ddwaf_obj_base, o) == 0);
#endif

namespace impl {
template <typename T>
constexpr bool is_ddwaf_obj_member() {
#if defined(__cpp_lib_is_pointer_interconvertible) && \
    defined(__cpp_lib_is_layout_compatible)
  return std::is_pointer_interconvertible_base_of_v<ddwaf_obj_base, T> &&
         std::is_corresponding_member<ddwaf_obj_base, T, ddwaf_object,
                                      ddwaf_object>(&ddwaf_obj_base::o, &T::o);
#elif defined(__clang__)
  // layout-compatible: the common initial sequence is all of both classes
  return __is_pointer_interconvertible_base_of(ddwaf_obj_base, T) &&
         __is_layout_compatible(ddwaf_obj_base, T);
#else
#error "no way to check pointer interconvertibility and common initial sequence"
#endif
}
}  // namespace impl

static_assert(impl::is_ddwaf_obj_member<ddwaf_str_obj>());
static_assert(impl::is_ddwaf_obj_member<ddwaf_bool_obj>());
static_assert(impl::is_ddwaf_obj_member<ddwaf_arr_obj>());
static_assert(impl::is_ddwaf_obj_member<ddwaf_map_obj>());

// Owns the memory of the object. Holds the union, never a bare member.
template <typename T = ddwaf_obj>
class ddwaf_owned_obj {  // NOLINT(readability-identifier-naming)
  ddwaf_obj obj_;
  DdwafMemres memres_;

 public:
  ddwaf_owned_obj() {
    if constexpr (std::same_as<T, ddwaf_map_obj>) {
      obj_.make_map(nullptr, 0);
    } else if constexpr (std::same_as<T, ddwaf_arr_obj>) {
      obj_.make_array(nullptr, 0);
    } else if constexpr (std::same_as<T, ddwaf_str_obj>) {
      obj_.make_string(std::string_view{});
    } else if constexpr (std::same_as<T, ddwaf_bool_obj>) {
      obj_.make_bool(false);
    }
  }
  // Throws if the object is not a T
  template <typename OT>
  explicit ddwaf_owned_obj(ddwaf_owned_obj<OT> &&oth)
      : obj_{checked(oth.obj())}, memres_{std::move(oth.memres())} {
    oth.obj().make_invalid();
  }
  template <typename OT>
  ddwaf_owned_obj<T> &operator=(ddwaf_owned_obj<OT> &&oth) = delete;
  template <typename OT>
  ddwaf_owned_obj(const ddwaf_owned_obj<OT> &) = delete;
  template <typename OT>
  ddwaf_owned_obj<T> &operator=(const ddwaf_owned_obj<OT> &) = delete;

  T &get() { return cast<T>(obj_); }
  const T &get() const { return cast<T>(obj_); }
  ddwaf_obj &obj() { return obj_; }
  const ddwaf_obj &obj() const { return obj_; }
  DdwafMemres &memres() { return memres_; }

 private:
  static const ddwaf_obj &checked(const ddwaf_obj &obj) {
    (void)cast<T>(obj);
    return obj;
  }
};

using ddwaf_owned_map = ddwaf_owned_obj<ddwaf_map_obj>;  // NOLINT

ddwaf_owned_obj<ddwaf_obj> json_to_object(
    const rapidjson::GenericValue<rapidjson::UTF8<>> &doc, int max_depth);

// For objects that libddwaf writes, with the default allocator, through out().
// Holds the union, never a bare member.
template <typename T = ddwaf_obj>
// NOLINTNEXTLINE(readability-identifier-naming)
class libddwaf_owned_ddwaf_obj {
 public:
  libddwaf_owned_ddwaf_obj() = default;
  libddwaf_owned_ddwaf_obj(const libddwaf_owned_ddwaf_obj &) = delete;
  libddwaf_owned_ddwaf_obj &operator=(const libddwaf_owned_ddwaf_obj &) =
      delete;
  libddwaf_owned_ddwaf_obj(libddwaf_owned_ddwaf_obj &&oth) noexcept
      : obj_{oth.synced()} {
    oth.obj_.make_invalid();
  }
  libddwaf_owned_ddwaf_obj &operator=(libddwaf_owned_ddwaf_obj &&oth) noexcept {
    if (this != &oth) {
      destroy();
      obj_ = oth.synced();
      oth.obj_.make_invalid();
    }
    return *this;
  }
  ~libddwaf_owned_ddwaf_obj() { destroy(); }

  // Where libddwaf writes its output
  ddwaf_object *out() noexcept { return &synced().raw(); }

  const ddwaf_obj &obj() const noexcept { return synced(); }

  // An empty T if the object is not a T (e.g. libddwaf did not write it)
  const T &get() const noexcept {
    const ddwaf_obj &obj = synced();
    if constexpr (std::same_as<T, ddwaf_obj>) {
      return obj;
    } else {
      if (!T::has_expected_type(obj.raw())) {
        return cast_unchecked<T>(empty());
      }
      return cast_unchecked<T>(obj);
    }
  }
  const T &operator*() const noexcept { return get(); }

 private:
  // libddwaf may have written the object since we last looked
  ddwaf_obj &synced() const noexcept {
    obj_.sync();
    return obj_;
  }

  void destroy() noexcept {
    ddwaf_object_destroy(&synced().raw(), ddwaf_get_default_allocator());
  }

  // rare: libddwaf did not write the object
  [[gnu::noinline, gnu::cold]] static const ddwaf_obj &empty() noexcept {
    static const ddwaf_owned_obj<T> kEmpty;
    return kEmpty.obj();
  }

  mutable ddwaf_obj obj_;
};

// Implementation: free functions, then ddwaf_obj, then ddwaf_kv_obj, the
// typed members, ddwaf_obj_base and the readers.

template <typename T>
T &cast(ddwaf_obj &obj) {
  if constexpr (std::same_as<T, ddwaf_obj>) {
    return obj;
  } else {
    if (!T::has_expected_type(obj.raw())) {
      throw std::invalid_argument("unexpected ddwaf object type");
    }
    return obj.member_unchecked<T>();
  }
}

template <typename T>
const T &cast(const ddwaf_obj &obj) {
  return cast<T>(const_cast<ddwaf_obj &>(obj));  // NOLINT
}

template <typename T>
const T &cast_unchecked(const ddwaf_obj &obj) noexcept {
  if constexpr (std::same_as<T, ddwaf_obj>) {
    return obj;
  } else {
    assert(T::has_expected_type(obj.raw()));
    return const_cast<ddwaf_obj &>(obj).member_unchecked<T>();  // NOLINT
  }
}

inline void ddwaf_obj::sync() noexcept {
  // With C++23, this can become std::start_lifetime_as<T>(&member), which
  // keeps the bytes without the copy below.
  // Reading any.o is allowed whatever member is active (common initial
  // sequence)
  ddwaf_object const contents = any.o;
  if (obj_read::is_map(contents)) {
    ::new (&map) ddwaf_map_obj(contents);
  } else if (obj_read::is_array(contents)) {
    ::new (&arr) ddwaf_arr_obj(contents);
  } else if (obj_read::is_string(contents)) {
    ::new (&str) ddwaf_str_obj(contents);
  } else if (obj_read::is_bool(contents)) {
    ::new (&b) ddwaf_bool_obj(contents);
  } else {
    ::new (&any) ddwaf_obj_base{contents};
  }
}

inline ddwaf_bool_obj &ddwaf_obj::make_bool(bool value) {
  ddwaf_bool_obj *obj = ::new (&b) ddwaf_bool_obj();
  obj->o.via.b8 = {.type = DDWAF_OBJ_BOOL, .val = value};
  return *obj;
}

template <typename T>
ddwaf_obj &ddwaf_obj::make_number(T value) {
  static_assert(std::is_arithmetic_v<T>, "T must be an arithmetic type");
  ::new (&any) ddwaf_obj_base{};
  if constexpr (std::is_floating_point_v<T>) {
    any.o.via.f64 = {.type = DDWAF_OBJ_FLOAT, .val = value};
  } else if constexpr (std::is_signed_v<T>) {
    any.o.via.i64 = {.type = DDWAF_OBJ_SIGNED, .val = value};
  } else {
    any.o.via.u64 = {.type = DDWAF_OBJ_UNSIGNED, .val = value};
  }
  return *this;
}

inline ddwaf_obj &ddwaf_obj::make_null() {
  ::new (&any) ddwaf_obj_base{};
  any.o.type = DDWAF_OBJ_NULL;
  return *this;
}

inline ddwaf_obj &ddwaf_obj::make_invalid() {
  ::new (&any) ddwaf_obj_base{};
  return *this;
}

inline ddwaf_str_obj &ddwaf_obj::make_string(std::string_view sv) {
  if (sv.size() > std::numeric_limits<uint32_t>::max()) {
    throw std::length_error("string too long for ddwaf object");
  }
  ddwaf_str_obj *obj = ::new (&str) ddwaf_str_obj();
  obj->o.via.str = {.type = DDWAF_OBJ_STRING,
                    .size = static_cast<uint32_t>(sv.size()),
                    .ptr = const_cast<char *>(sv.data())};  // NOLINT
  return *obj;
}

inline ddwaf_str_obj &ddwaf_obj::make_string(std::string_view sv,
                                             DdwafMemres &memres) {
  return make_string(memres.copy_string(sv));
}

inline ddwaf_arr_obj &ddwaf_obj::make_array(ddwaf_obj *elems,
                                            std::size_t size) {
  ddwaf_arr_obj *obj = ::new (&arr) ddwaf_arr_obj();
  obj->set_array(elems, size);
  return *obj;
}

inline ddwaf_arr_obj &ddwaf_obj::make_array(std::size_t size,
                                            DdwafMemres &memres) {
  return make_array(memres.allocate_objects<ddwaf_obj>(size), size);
}

inline ddwaf_map_obj &ddwaf_obj::make_map(ddwaf_kv_obj *entries,
                                          std::size_t size) {
  ddwaf_map_obj *obj = ::new (&map) ddwaf_map_obj();
  obj->set_map(entries, size);
  return *obj;
}

inline ddwaf_map_obj &ddwaf_obj::make_map(std::size_t size,
                                          DdwafMemres &memres) {
  return make_map(memres.allocate_kvs(size), size);
}

inline ddwaf_obj_base &ddwaf_obj::active() noexcept {
  // Reading any.o is allowed whatever member is active (common initial
  // sequence)
  const ddwaf_object &o = any.o;
  if (obj_read::is_map(o)) {
    return map;
  }
  if (obj_read::is_array(o)) {
    return arr;
  }
  if (obj_read::is_string(o)) {
    return str;
  }
  if (obj_read::is_bool(o)) {
    return b;
  }
  return any;
}

template <typename T>
T &ddwaf_obj::member_unchecked() noexcept {
  if constexpr (std::same_as<T, ddwaf_map_obj>) {
    return map;
  } else if constexpr (std::same_as<T, ddwaf_arr_obj>) {
    return arr;
  } else if constexpr (std::same_as<T, ddwaf_str_obj>) {
    return str;
  } else {
    static_assert(std::same_as<T, ddwaf_bool_obj>);
    return b;
  }
}

inline ddwaf_kv_obj &ddwaf_kv_obj::set_key(std::string_view sv) {
  key_obj.make_string(sv);
  return *this;
}

inline ddwaf_kv_obj &ddwaf_kv_obj::set_key(std::string_view sv,
                                           DdwafMemres &memres) {
  key_obj.make_string(sv, memres);
  return *this;
}

template <typename T>
T &ddwaf_map_obj::get(std::string_view key) {
  T *value = get_opt<T>(key);
  if (value == nullptr) {
    throw std::out_of_range(std::string{"key "} + std::string{key} +
                            " not found");
  }
  return *value;
}

template <typename T>
T *ddwaf_map_obj::get_opt(std::string_view key) {
  for (ddwaf_kv_obj &entry : *this) {
    if (entry.key() == key) {
      return &cast<T>(entry.val);
    }
  }
  return nullptr;
}

inline ddwaf_kv_obj &ddwaf_map_obj::at_unchecked(std::size_t index) const {
  return begin()[index];
}

inline ddwaf_kv_obj *ddwaf_map_obj::end() const noexcept {
  return begin() + size();
}

template <typename T>
T &ddwaf_arr_obj::at(std::size_t index) const {
  if (index >= size()) {
    throw std::out_of_range("index out of range");
  }
  return at_unchecked<T>(index);
}

template <typename T>
T &ddwaf_arr_obj::at_unchecked(std::size_t index) const {
  return const_cast<T &>(
      cast_unchecked<T>(array_ptr_unchecked()[index]));  // NOLINT
}

inline ddwaf_obj &ddwaf_arr_obj::append() {
  std::size_t const index = size();
  if (index >= obj_read::capacity_unchecked(o)) {
    throw std::length_error("ddwaf array is full");
  }
  set_size_unchecked(index + 1);
  return at_unchecked(index);
}

inline ddwaf_obj *ddwaf_arr_obj::end() const noexcept {
  return array_ptr_unchecked() + size();
}

// Container fields are written and read through the `via` member that
// matches the type, as libddwaf does: `ptr` is not in the common initial
// sequence of these members.
//
// The container pointers cross the interface with libddwaf. libddwaf reads
// our arrays as ddwaf_object[] and our maps as ddwaf_object_kv[], and we
// read its containers as ddwaf_obj[] and ddwaf_kv_obj[]. The standard does
// not allow this: those are not the types of the objects there. It works
// because the layouts are the same (see the static_asserts after
// ddwaf_kv_obj) and libddwaf is compiled separately, so no optimizer sees
// both sides.

inline void ddwaf_obj_base::set_array(ddwaf_obj *elems, std::size_t size) {
  auto *ptr = reinterpret_cast<ddwaf_object *>(elems);  // NOLINT
  if (size <= kMaxCompactSize) {
    auto const compact_size = static_cast<uint16_t>(size);
    o.via.array = {.type = DDWAF_OBJ_ARRAY,
                   .size = compact_size,
                   .capacity = compact_size,
                   .ptr = ptr};
    return;
  }
  check_large_size(size);
  o.via.large_array = {._type = DDWAF_OBJ_LARGE_ARRAY,
                       .size = size,
                       .capacity = size,
                       .ptr = ptr};
}

inline void ddwaf_obj_base::set_map(ddwaf_kv_obj *entries, std::size_t size) {
  auto *ptr = reinterpret_cast<ddwaf_object_kv *>(entries);  // NOLINT
  if (size <= kMaxCompactSize) {
    auto const compact_size = static_cast<uint16_t>(size);
    o.via.map = {.type = DDWAF_OBJ_MAP,
                 .size = compact_size,
                 .capacity = compact_size,
                 .ptr = ptr};
    return;
  }
  check_large_size(size);
  o.via.large_map = {
      ._type = DDWAF_OBJ_LARGE_MAP, .size = size, .capacity = size, .ptr = ptr};
}

inline void ddwaf_obj_base::set_size_unchecked(std::size_t size) noexcept {
  assert(size <= obj_read::capacity_unchecked(o));
  if (o.type == DDWAF_OBJ_LARGE_ARRAY) {
    o.via.large_array.size = size;
  } else if (o.type == DDWAF_OBJ_LARGE_MAP) {
    o.via.large_map.size = size;
  } else if (o.type == DDWAF_OBJ_MAP) {
    o.via.map.size = static_cast<uint16_t>(size);
  } else {
    o.via.array.size = static_cast<uint16_t>(size);
  }
}

inline ddwaf_obj *ddwaf_obj_base::array_ptr_unchecked() const noexcept {
  ddwaf_object *ptr =
      o.type == DDWAF_OBJ_LARGE_ARRAY ? o.via.large_array.ptr : o.via.array.ptr;
  return reinterpret_cast<ddwaf_obj *>(ptr);  // NOLINT
}

inline ddwaf_kv_obj *ddwaf_obj_base::map_ptr_unchecked() const noexcept {
  ddwaf_object_kv *ptr =
      o.type == DDWAF_OBJ_LARGE_MAP ? o.via.large_map.ptr : o.via.map.ptr;
  return reinterpret_cast<ddwaf_kv_obj *>(ptr);  // NOLINT
}

inline void ddwaf_obj_base::check_large_size(std::size_t size) {
  if (size > kMaxLargeSize) {
    throw std::length_error("container too large for ddwaf object");
  }
}

namespace obj_read {
inline bool is_numeric(const ddwaf_object &o) noexcept {
  return o.type == DDWAF_OBJ_SIGNED || o.type == DDWAF_OBJ_UNSIGNED ||
         o.type == DDWAF_OBJ_FLOAT;
}

inline bool is_bool(const ddwaf_object &o) noexcept {
  return o.type == DDWAF_OBJ_BOOL;
}

inline bool is_null(const ddwaf_object &o) noexcept {
  return o.type == DDWAF_OBJ_NULL;
}

inline bool is_string(const ddwaf_object &o) noexcept {
  return o.type == DDWAF_OBJ_STRING || o.type == DDWAF_OBJ_LITERAL_STRING ||
         o.type == DDWAF_OBJ_SMALL_STRING;
}

inline bool is_map(const ddwaf_object &o) noexcept {
  return o.type == DDWAF_OBJ_MAP || o.type == DDWAF_OBJ_LARGE_MAP;
}

inline bool is_array(const ddwaf_object &o) noexcept {
  return o.type == DDWAF_OBJ_ARRAY || o.type == DDWAF_OBJ_LARGE_ARRAY;
}

inline std::size_t size_unchecked(const ddwaf_object &o) noexcept {
  if (o.type == DDWAF_OBJ_LARGE_ARRAY) {
    return o.via.large_array.size;
  }
  if (o.type == DDWAF_OBJ_LARGE_MAP) {
    return o.via.large_map.size;
  }
  if (o.type == DDWAF_OBJ_MAP) {
    return o.via.map.size;
  }
  return o.via.array.size;
}

inline std::size_t capacity_unchecked(const ddwaf_object &o) noexcept {
  if (o.type == DDWAF_OBJ_LARGE_ARRAY) {
    return o.via.large_array.capacity;
  }
  if (o.type == DDWAF_OBJ_LARGE_MAP) {
    return o.via.large_map.capacity;
  }
  if (o.type == DDWAF_OBJ_MAP) {
    return o.via.map.capacity;
  }
  return o.via.array.capacity;
}

inline std::string_view string_val(const ddwaf_object &o) noexcept {
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

template <typename T>
T numeric_val(const ddwaf_object &o) {
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
      adjusted_max = 9223372036854774784.0;  // nextafter((double)max, -DBL_MAX)
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
}  // namespace obj_read

}  // namespace datadog::nginx::security
