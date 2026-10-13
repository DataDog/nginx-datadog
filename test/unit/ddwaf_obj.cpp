#include <security/ddwaf_obj.h>

#include <catch2/catch_test_macros.hpp>

namespace dnsec = datadog::nginx::security;
using dnsec::ddwaf_obj;

TEST_CASE("numeric_val<double> accepts zero and negative values",
          "[ddwaf_obj]") {
  ddwaf_obj obj;
  CHECK(obj.make_number(int64_t{0}).numeric_val<double>() == 0.0);
  CHECK(obj.make_number(int64_t{-5}).numeric_val<double>() == -5.0);
  CHECK(obj.make_number(0.0).numeric_val<double>() == 0.0);
  CHECK(obj.make_number(-2.5).numeric_val<double>() == -2.5);
}
