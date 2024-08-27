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
      .value = dnsec::ngx_stringv("application/json"sv)};
  static ngx_http_request_t req{.connection = &empty_conn,
                                .headers_in = {.content_type = &content_type}};
  ddwaf_obj slot;

  test::ManagedChain chain{parts};
  bool success = datadog::nginx::security::parse_body(slot, req, chain,
                                                      chain.size(), memres);

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

  auto arr = dnsec::ddwaf_arr_obj{*slot};
  CHECK(arr.size() == 9);

  auto first = arr.at_unchecked(0);
  CHECK((first.is_numeric() && first.numeric_val<int>() == 1));

  auto second = arr.at_unchecked(1);
  CHECK((second.is_numeric() && second.numeric_val<int>() == -1));

  auto third = arr.at_unchecked(2);
  CHECK((third.is_numeric() && third.numeric_val<double>() == 0.5));

  auto fourth = arr.at_unchecked(3);
  CHECK((fourth.is_bool() && fourth.boolean == true));

  auto fifth = arr.at_unchecked(4);
  CHECK((fifth.is_bool() && fifth.boolean == false));

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

  auto arr = dnsec::ddwaf_arr_obj{*slot};
  CHECK(arr.size() == 4);
  auto inner_arr = arr.at_unchecked(3);
  CHECK(inner_arr.is_array());
  CHECK(inner_arr.size_unchecked() == 2);
}
