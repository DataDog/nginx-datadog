#include <security/ddwaf_obj.h>

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
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
  CHECK(map.get("a"sv).string_val() == "1");
  CHECK(map.get("b"sv).numeric_val<int>() == 2);
  CHECK(map.get_opt("c"sv) == nullptr);
  CHECK((*map.begin()).key() == "a");
}

TEST_CASE("small strings are read in place", "[ddwaf_obj]") {
  ddwaf_obj obj;
  // one reference: raw() picks the member from the type tag we are changing
  ddwaf_object &o = obj.raw();
  o.via.sstr.type = DDWAF_OBJ_SMALL_STRING;
  o.via.sstr.size = 5;
  std::memcpy(o.via.sstr.data, "hello", 5);
  obj.sync();  // written other than through make_*()

  CHECK(obj.is_string());
  CHECK(obj.string_val() == "hello");
}

TEST_CASE("containers over 65535 elements are large", "[ddwaf_obj]") {
  dnsec::DdwafMemres memres;
  ddwaf_obj obj;
  ddwaf_arr_obj &arr = obj.make_array(70000, memres);

  CHECK(obj.raw().type == DDWAF_OBJ_LARGE_ARRAY);
  CHECK(obj.is_array());
  CHECK(arr.size() == 70000);
  arr.at_unchecked(69999).make_number(7);
  CHECK(arr.at(69999).numeric_val<int>() == 7);

  ddwaf_obj map_obj;
  ddwaf_map_obj &map = map_obj.make_map(70000, memres);
  CHECK(map_obj.raw().type == DDWAF_OBJ_LARGE_MAP);
  CHECK(map.size() == 70000);
}

TEST_CASE("append fills an array up to its capacity", "[ddwaf_obj]") {
  dnsec::DdwafMemres memres;
  ddwaf_obj obj;
  ddwaf_arr_obj &arr = obj.make_array(2, memres);
  arr.clear();

  arr.append().make_number(1);
  arr.append().make_number(2);
  CHECK(arr.size() == 2);
  CHECK(arr.at(1).numeric_val<int>() == 2);
  CHECK_THROWS_AS(arr.append(), std::length_error);
  CHECK(arr.size() == 2);

  arr.clear();
  CHECK(arr.empty());
  arr.append().make_number(3);
  CHECK(arr.at(0).numeric_val<int>() == 3);
}

TEST_CASE("numeric_val<double> accepts zero and negative values",
          "[ddwaf_obj]") {
  ddwaf_obj obj;
  CHECK(obj.make_number(int64_t{0}).numeric_val<double>() == 0.0);
  CHECK(obj.make_number(int64_t{-5}).numeric_val<double>() == -5.0);
  CHECK(obj.make_number(0.0).numeric_val<double>() == 0.0);
  CHECK(obj.make_number(-2.5).numeric_val<double>() == -2.5);
}
