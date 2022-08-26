#pragma once

// This component provides extensions to the `opentracing::string_view` type,
// which is itself a shim of C++17's `std::string_view` type.
//
// `datadog::nginx::string_view` is a type alias for
// `opentracing::string_view`.  This header file also declares extensions to
// the type, such as a specialization of `std::hash`.


#include <functional>

namespace datadog {
namespace nginx {

using string_view = ::opentracing::string_view;

}  // namespace nginx
}  // namespace datadog

namespace std {

template <>
struct hash<::datadog::nginx::string_view> {
  // Return the hash of the specified `string`.
  std::size_t operator()(opentracing::string_view string) const {
    return hash_bytes(string.data(), string.size());
  }

  // Return the hash of the specified array of characters
  // `[begin, begin + size)`.
  static long hash_bytes(const char* begin, std::size_t size);
};

}  // namespace std
