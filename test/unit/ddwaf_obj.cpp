#include <security/ddwaf_obj.h>

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

namespace dnsec = datadog::nginx::security;
using dnsec::ddwaf_arr_obj;
using dnsec::ddwaf_kv_obj;
using dnsec::ddwaf_map_obj;
using dnsec::ddwaf_obj;

using namespace std::literals::string_view_literals;

TEST_CASE("map entries hold the key and the value", "[ddwaf_obj]") {
  dnsec::DdwafMemres memres;
  ddwaf_obj obj;
  ddwaf_map_obj &map = obj.make_map(2, memres);
  map.at_unchecked(0).set_key("a"sv).val.make_string("1"sv);
  map.at_unchecked(1).set_key("b"sv, memres).val.make_number(2);

  REQUIRE(obj.is_map());
  CHECK(map.size() == 2);
  CHECK(map.get("a"sv).string_val_unchecked() == "1");
  CHECK(map.get("b"sv).numeric_val<int>() == 2);
  CHECK(map.get_opt("c"sv) == nullptr);
  CHECK((*map.begin()).key() == "a");
}

TEST_CASE("small strings are read in place", "[ddwaf_obj]") {
  ddwaf_obj obj;
  obj.o.via.sstr.type = DDWAF_OBJ_SMALL_STRING;
  obj.o.via.sstr.size = 5;
  std::memcpy(obj.o.via.sstr.data, "hello", 5);

  CHECK(obj.is_string());
  CHECK(obj.string_val_unchecked() == "hello");
}

TEST_CASE("containers over 65535 elements are large", "[ddwaf_obj]") {
  dnsec::DdwafMemres memres;
  ddwaf_obj obj;
  ddwaf_arr_obj &arr = obj.make_array(70000, memres);

  CHECK(obj.o.type == DDWAF_OBJ_LARGE_ARRAY);
  CHECK(obj.is_array());
  CHECK(arr.size() == 70000);
  arr.at_unchecked(69999).make_number(7);
  CHECK(arr.at(69999).numeric_val<int>() == 7);

  ddwaf_obj map_obj;
  ddwaf_map_obj &map = map_obj.make_map(70000, memres);
  CHECK(map_obj.o.type == DDWAF_OBJ_LARGE_MAP);
  CHECK(map.size() == 70000);
}
