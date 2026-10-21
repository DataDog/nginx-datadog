#include <catch2/catch_test_macros.hpp>
#include <string>

#include "managed_chain.h"
#include "security/body_parse/body_parsing.h"
#include "security/util.h"

extern "C" {
#include <ngx_http.h>
}

namespace dnsec = datadog::nginx::security;
using dnsec::ddwaf_obj;

using namespace std::literals::string_view_literals;

namespace {
std::optional<ddwaf_obj> parse(
    std::vector<std::string_view> parts, dnsec::DdwafMemres &memres,
    std::size_t budget_limit = dnsec::WafInputBudget::kDefaultLimit) {
  static ngx_log_t log{};
  static ngx_connection_t empty_conn{.log = &log};
  static ngx_table_elt_t content_type = {
      .value = dnsec::ngx_stringv("application/json"sv)};
  static ngx_http_request_t req{.connection = &empty_conn,
                                .headers_in = {.content_type = &content_type}};
  ddwaf_obj slot;

  test::ManagedChain chain{parts};
  dnsec::WafInputBudget budget{budget_limit};
  bool success = datadog::nginx::security::parse_body_req(
      slot, req, chain, chain.size(), budget, memres);

  if (!success) {
    return std::nullopt;
  }

  return {slot};
}
}  // namespace

TEST_CASE("all the types", "[json]") {
  std::vector parts = {
      "[1, -1, 0.5, true, false, null, 8589934592, -8589934592, {}]"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);
  REQUIRE((slot && slot->is_array()));

  const dnsec::ddwaf_arr_obj &arr = dnsec::cast(*slot);
  CHECK(arr.size() == 9);

  auto first = arr.at_unchecked(0);
  CHECK((first.is_numeric() && first.numeric_val<int>() == 1));

  auto second = arr.at_unchecked(1);
  CHECK((second.is_numeric() && second.numeric_val<int>() == -1));

  auto third = arr.at_unchecked(2);
  CHECK((third.is_numeric() && third.numeric_val<double>() == 0.5));

  auto fourth = arr.at_unchecked(3);
  CHECK((fourth.is_bool() && fourth.raw().via.b8.val == true));

  auto fifth = arr.at_unchecked(4);
  CHECK((fifth.is_bool() && fifth.raw().via.b8.val == false));

  auto sixth = arr.at_unchecked(5);
  CHECK(sixth.is_null());

  auto seventh = arr.at_unchecked(6);
  CHECK((seventh.is_numeric() && seventh.numeric_val<int64_t>() == 8589934592));

  auto eighth = arr.at_unchecked(7);
  CHECK((eighth.is_numeric() && eighth.numeric_val<int64_t>() == -8589934592));
}

TEST_CASE("pool object recycling", "[json]") {
  // allocates array of size 2 upon seeing 2, and returns it upon seeing 3
  // inner array reuses the returned buffer
  std::vector parts = {"[1, 2, 3, [1, 2]]"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);
  REQUIRE((slot && slot->is_array()));

  const dnsec::ddwaf_arr_obj &arr = dnsec::cast(*slot);
  CHECK(arr.size() == 4);
  auto inner_arr = arr.at_unchecked(3);
  CHECK(inner_arr.is_array());
  CHECK(inner_arr.size_unchecked() == 2);
}

TEST_CASE("objects", "[json]") {
  std::vector parts = {R"({"a": 1, "b": {"c": "d"}, "e": [true]})"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);
  REQUIRE((slot && slot->is_map()));

  const dnsec::ddwaf_map_obj &map = dnsec::cast(*slot);
  CHECK(map.size() == 3);
  CHECK(map.get("a").numeric_val<int>() == 1);
  auto &inner = map.get<dnsec::ddwaf_map_obj>("b");
  CHECK(inner.size() == 1);
  CHECK(inner.get("c").string_val() == "d");
  CHECK(map.get<dnsec::ddwaf_arr_obj>("e").size() == 1);
}

TEST_CASE("object truncated after a key", "[json]") {
  std::vector parts = {R"({"a": "b", "c")"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);
  REQUIRE((slot && slot->is_map()));

  // the key is kept, so rules on keys still see it
  const dnsec::ddwaf_map_obj &map = dnsec::cast(*slot);
  REQUIRE(map.size() == 2);
  CHECK(map.at_unchecked(0).key() == "a");
  CHECK(map.at_unchecked(0).val.string_val() == "b");
  CHECK(map.at_unchecked(1).key() == "c");
  CHECK(map.at_unchecked(1).val.is_null());
}

// The examples of RFC-1089, with a budget of 64. The RFC prefers a
// breadth-first walk, but we parse depth-first, so examples 4 and 7 differ.
TEST_CASE("budget: object that fits exactly", "[json]") {
  // costs: map 4; each key 8; "Alice" 7; "San Francisco" 15; "engineer" 10.
  // That is 60, so this uses 60 instead of the RFC's 64.
  std::vector parts = {
      R"({"name": "Alice", "city": "San Francisco", "role": "engineer"})"sv};

  SECTION("a budget of exactly its cost") {
    dnsec::DdwafMemres memres;
    auto slot = parse(parts, memres, 60);
    REQUIRE((slot && slot->is_map()));
    const dnsec::ddwaf_map_obj &map = dnsec::cast(*slot);
    CHECK(map.size() == 3);
    CHECK(map.get("role").string_val() == "engineer");
  }

  SECTION("one less: the last string is cut") {
    dnsec::DdwafMemres memres;
    auto slot = parse(parts, memres, 59);
    REQUIRE((slot && slot->is_map()));
    const dnsec::ddwaf_map_obj &map = dnsec::cast(*slot);
    CHECK(map.size() == 3);
    CHECK(map.get("role").string_val() == "enginee");
  }
}

TEST_CASE("budget: array without room for elements", "[json]") {
  std::vector parts = {
      R"({"user": {"first": "Bob", "last": "Smith"}, "scores": [95, 87, 73]})"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres, 64);
  REQUIRE((slot && slot->is_map()));

  const dnsec::ddwaf_map_obj &map = dnsec::cast(*slot);
  REQUIRE(map.size() == 2);
  CHECK(map.get<dnsec::ddwaf_map_obj>("user").size() == 2);
  CHECK(map.get<dnsec::ddwaf_arr_obj>("scores").size() == 0);
}

TEST_CASE("budget: string is truncated", "[json]") {
  std::vector parts = {
      R"(["hello", "world", "this is a longer string that will be cut"])"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres, 64);
  REQUIRE((slot && slot->is_array()));

  const dnsec::ddwaf_arr_obj &arr = dnsec::cast(*slot);
  REQUIRE(arr.size() == 3);
  CHECK(arr.at_unchecked(2).string_val() ==
        "this is a longer string that will be c");
}

TEST_CASE("budget: map entries are dropped", "[json]") {
  std::string json = "{";
  for (int i = 0; i < 20; i++) {
    json += (i > 0 ? ", " : "") + ("\"" + std::to_string(i) + "\": ") +
            std::to_string(i);
  }
  json += "}";
  std::vector parts = {std::string_view{json}};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres, 64);
  REQUIRE((slot && slot->is_map()));

  // The RFC drops the 7th entry. We keep its key, with a null value, as
  // we do for truncated input.
  const dnsec::ddwaf_map_obj &map = dnsec::cast(*slot);
  REQUIRE(map.size() == 7);
  CHECK(map.get("5").numeric_val<int>() == 5);
  CHECK(map.get("6").is_null());
}

TEST_CASE("budget: nested objects, depth-first", "[json]") {
  std::vector parts = {
      R"({"a": {"b": {"c": {"d": {"e": "deep"}}}}, "extra": "data", "more": [1, 2, 3]})"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres, 64);
  REQUIRE((slot && slot->is_map()));

  // the RFC (breadth-first) gives {"a": {"b": {}}, "extra": "data",
  // "more": [1, 2]}
  const dnsec::ddwaf_map_obj &map = dnsec::cast(*slot);
  REQUIRE(map.size() == 2);
  CHECK(map.get<dnsec::ddwaf_map_obj>("a").size() == 1);
  CHECK(map.get("extra").string_val() == "da");
}

TEST_CASE("budget: parsing stops after a truncated string", "[json]") {
  std::vector parts = {
      R"({"flag": true, "count": 42, "tags": ["python", "serialization"], "meta": null})"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres, 64);
  REQUIRE((slot && slot->is_map()));

  // the RFC (breadth-first) keeps "meta" and drops "serialization"
  const dnsec::ddwaf_map_obj &map = dnsec::cast(*slot);
  REQUIRE(map.size() == 3);
  auto &tags = map.get<dnsec::ddwaf_arr_obj>("tags");
  REQUIRE(tags.size() == 2);
  CHECK(tags.at_unchecked(1).string_val() == "serializa");
}

namespace {
// Follows the first element of each container, without recursion
std::size_t depth_of_first_path(const ddwaf_obj &obj) {
  std::size_t depth = 0;
  const ddwaf_obj *cur = &obj;
  while (cur->is_array() || cur->is_map()) {
    depth++;
    if (cur->size_unchecked() == 0) {
      break;
    }
    if (cur->is_array()) {
      cur = &dnsec::cast<dnsec::ddwaf_arr_obj>(*cur).at_unchecked(0);
    } else {
      cur = &dnsec::cast<dnsec::ddwaf_map_obj>(*cur).at_unchecked(0).val;
    }
  }
  return depth;
}
}  // namespace

TEST_CASE("too-deep containers are kept empty", "[json]") {
  // the whole raw limit of '[', then a sibling at the top level
  std::size_t const nesting = 40 * 1024;
  std::string json =
      "[" + std::string(nesting, '[') + std::string(nesting, ']') + ", 2]";
  std::vector parts = {std::string_view{json}};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);
  REQUIRE((slot && slot->is_array()));

  // the last container is too deep, so it is empty
  CHECK(depth_of_first_path(*slot) ==
        dnsec::WafInputBudget::kMaxContainerDepth + 1);
  const dnsec::ddwaf_arr_obj &arr = dnsec::cast(*slot);
  REQUIRE(arr.size() == 2);
  CHECK(arr.at_unchecked(1).numeric_val<int>() == 2);
}

TEST_CASE("contents of too-deep maps are dropped", "[json]") {
  std::string json;
  for (int i = 0; i < 30; i++) {
    json += R"({"a": )";
  }
  json += "1";
  for (int i = 0; i < 30; i++) {
    json += R"(, "k": "v"})";
  }
  std::vector parts = {std::string_view{json}};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);
  REQUIRE((slot && slot->is_map()));

  // the last container is too deep, so it is empty
  CHECK(depth_of_first_path(*slot) ==
        dnsec::WafInputBudget::kMaxContainerDepth + 1);
  // the top level is complete
  const dnsec::ddwaf_map_obj &map = dnsec::cast(*slot);
  REQUIRE(map.size() == 2);
  CHECK(map.get("k").string_val() == "v");
}
