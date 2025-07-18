#include <catch2/catch_test_macros.hpp>

#include "managed_chain.h"
#include "security/body_parse/body_parsing.h"

extern "C" {
#include <ngx_http.h>
}

namespace dnsec = datadog::nginx::security;
using dnsec::ddwaf_obj;

using namespace std::literals::string_view_literals;

namespace {
std::optional<ddwaf_obj> parse(std::vector<std::string_view> parts,
                               dnsec::DdwafMemres &memres) {
  static ngx_log_t log{};
  static ngx_connection_t empty_conn{.log = &log};
  static ngx_table_elt_t content_type = {
      .value = dnsec::ngx_stringv("application/x-www-form-urlencoded"sv)};
  static ngx_http_request_t req{.connection = &empty_conn,
                                .headers_in = {.content_type = &content_type}};
  ddwaf_obj slot;

  test::ManagedChain chain{parts};
  bool success = datadog::nginx::security::parse_body_req(slot, req, chain,
                                                          chain.size(), memres);

  if (!success) {
    return std::nullopt;
  }

  return {slot};
}
}  // namespace

TEST_CASE("urlencoded empty data", "[urlencoded]") {
  std::vector parts = {""sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);
  REQUIRE((slot && slot->is_map()));
  CHECK(slot->size_unchecked() == 0);
}

TEST_CASE("urlencoded simple key-pair", "[urlencoded]") {
  std::vector<std::string_view> parts = {"key=value"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);

  REQUIRE((slot && slot->is_map()));
  dnsec::ddwaf_map_obj value{*slot};
  CHECK(value.size() == 1);

  auto maybe_value = value.get_opt("key");
  REQUIRE(maybe_value);
  REQUIRE(maybe_value->is_string());
  CHECK(maybe_value->string_val_unchecked() == "value");
}

TEST_CASE("urlencoded repeated key", "[urlencoded]") {
  std::vector parts = {"key=value1&key=value2&"sv,
                       "foo=bar1&key=value3&foo=bar2"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);

  REQUIRE((slot && slot->is_map()));
  dnsec::ddwaf_map_obj value{*slot};
  REQUIRE(value.size() == 2);

  auto key_values = value.get_opt("key");
  auto foo_values = value.get_opt("foo");

  REQUIRE(key_values);
  REQUIRE(key_values->is_array());
  dnsec::ddwaf_arr_obj key_arr{*key_values};
  CHECK(key_arr.size() == 3);
  CHECK(key_arr.at_unchecked(0).string_val_unchecked() == "value1");
  CHECK(key_arr.at_unchecked(1).string_val_unchecked() == "value2");
  CHECK(key_arr.at_unchecked(2).string_val_unchecked() == "value3");

  REQUIRE(foo_values);
  REQUIRE(foo_values->is_array());
  dnsec::ddwaf_arr_obj foo_arr{*foo_values};
  CHECK(foo_arr.size() == 2);
  CHECK(foo_arr.at_unchecked(0).string_val_unchecked() == "bar1");
  CHECK(foo_arr.at_unchecked(1).string_val_unchecked() == "bar2");
}

TEST_CASE("url plus decoding", "[urlencoded]") {
  std::vector parts = {"key+%20=value+%20"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);

  REQUIRE((slot && slot->is_map()));
  auto maybe_value = dnsec::ddwaf_map_obj{*slot}.get_opt("key  ");
  REQUIRE(maybe_value);
  REQUIRE(maybe_value->is_string());
  CHECK(maybe_value->string_val_unchecked() == "value  ");
}

TEST_CASE("multiple equal signs", "[urlencoded]") {
  std::vector parts = {"key=value=value"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);

  REQUIRE((slot && slot->is_map()));
  auto maybe_value = dnsec::ddwaf_map_obj{*slot}.get_opt("key");
  REQUIRE(maybe_value);
  REQUIRE(maybe_value->is_string());
  CHECK(maybe_value->string_val_unchecked() == "value=value");
}

TEST_CASE("no equal sign", "[urlencoded]") {
  std::vector parts = {"key&key=value2&"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);

  REQUIRE((slot && slot->is_map()));
  auto maybe_value = dnsec::ddwaf_map_obj{*slot}.get_opt("key");
  REQUIRE(maybe_value);
  REQUIRE(maybe_value->is_array());
  dnsec::ddwaf_arr_obj value_arr{*maybe_value};
  CHECK(value_arr.size() == 2);
  REQUIRE(value_arr.at(0).is_string());
  CHECK(value_arr.at(0).string_val_unchecked() == "");
  REQUIRE(value_arr.at(1).is_string());
  CHECK(value_arr.at(1).string_val_unchecked() == "value2");
}

TEST_CASE("empty key", "[urlencoded]") {
  std::vector parts = {"=value"sv};
  dnsec::DdwafMemres memres;
  auto slot = parse(parts, memres);

  REQUIRE((slot && slot->is_map()));
  auto maybe_value = dnsec::ddwaf_map_obj{*slot}.get_opt("");
  REQUIRE(maybe_value);
  REQUIRE(maybe_value->is_string());
  CHECK(maybe_value->string_val_unchecked() == "value");
}
