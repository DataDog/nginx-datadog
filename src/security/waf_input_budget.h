#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace datadog::nginx::security {

// The size budget of RFC-1089 for a parsed body. Parsers charge it while they
// build the ddwaf objects, and stop when a charge fails.
//
// Costs:
// - string: 2 + its length;
// - array: 4, plus 2 per element, plus the elements;
// - map: 4, plus 2 per entry, plus the keys and values. Keys are strings, so
//   each costs 2 + its length;
// - other values: 4.
//
// So a map entry costs 2 + (2 + key length) + its value.
//
// The 2 per element or entry is charged with the element, or with the key of
// the entry. Pass element = true for these.
//
// Once a charge fails, or a string is truncated, the budget is exhausted and
// all later charges fail.
class WafInputBudget {
 public:
  // Twice the limit on the raw body (Context::kMaxFilterData, 40 KiB).
  // Parsed objects can cost up to 3 times their raw text: [0,0,0] is 2 bytes
  // per element but costs 6. Typical JSON costs 1 to 1.5 times its raw size,
  // so it is not cut. Bodies made mostly of tiny values are cut.
  static constexpr std::size_t kDefaultLimit = 80 * 1024;

  // Containers deeper than this are kept empty. The top-level container has
  // depth 1. This way, code that walks the input recursively cannot overflow
  // the stack.
  static constexpr std::size_t kMaxContainerDepth = 21;

  explicit WafInputBudget(std::size_t limit = kDefaultLimit) noexcept
      : left_{limit} {}

  bool charge_scalar(bool element) noexcept {
    return charge(kScalarCost + slot_cost(element));
  }

  // Charges only the container. Its elements are charged as they are added.
  bool charge_container(bool element) noexcept {
    return charge(kContainerCost + slot_cost(element));
  }

  // Returns the part of the string that fits, or nullopt if none does. Does
  // not cut a UTF-8 character in the middle.
  std::optional<std::string_view> charge_string(std::string_view str,
                                                bool element) noexcept {
    std::size_t const fixed = kStringCost + slot_cost(element);
    if (exhausted_ || left_ < fixed) {
      exhausted_ = true;
      return std::nullopt;
    }
    left_ -= fixed;
    if (str.size() <= left_) {
      left_ -= str.size();
      return str;
    }

    std::size_t length = left_;
    // UTF-8 continuation bytes are 10xxxxxx
    while (length > 0 &&
           (static_cast<unsigned char>(str[length]) & 0xC0U) == 0x80U) {
      length--;
    }
    left_ = 0;
    exhausted_ = true;
    if (length == 0) {
      return std::nullopt;
    }
    return str.substr(0, length);
  }

  bool exhausted() const noexcept { return exhausted_; }

 private:
  static constexpr std::size_t kSlotCost = 2;
  static constexpr std::size_t kStringCost = 2;
  static constexpr std::size_t kContainerCost = 4;
  static constexpr std::size_t kScalarCost = 4;

  static std::size_t slot_cost(bool element) noexcept {
    return element ? kSlotCost : 0;
  }

  bool charge(std::size_t cost) noexcept {
    if (exhausted_ || left_ < cost) {
      exhausted_ = true;
      return false;
    }
    left_ -= cost;
    return true;
  }

  std::size_t left_;
  bool exhausted_{false};
};

}  // namespace datadog::nginx::security
