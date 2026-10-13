#include <security/waf_input_budget.h>

#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace dnsec = datadog::nginx::security;
using dnsec::WafInputBudget;

using namespace std::literals::string_view_literals;

TEST_CASE("elements cost 2 more", "[waf_input_budget]") {
  WafInputBudget budget{10};
  CHECK(budget.charge_scalar(true));      // 6
  CHECK(budget.charge_container(false));  // 4
  CHECK_FALSE(budget.charge_scalar(false));
  CHECK(budget.exhausted());
}

TEST_CASE("a failed charge exhausts the budget", "[waf_input_budget]") {
  WafInputBudget budget{5};
  CHECK_FALSE(budget.charge_container(true));
  // a scalar would fit, but we stopped
  CHECK_FALSE(budget.charge_scalar(false));
}

TEST_CASE("strings are truncated to fit", "[waf_input_budget]") {
  WafInputBudget budget{7};
  auto kept = budget.charge_string("abcdefgh"sv, false);
  REQUIRE(kept);
  CHECK(*kept == "abcde");
  CHECK(budget.exhausted());
  CHECK_FALSE(budget.charge_string(""sv, false));
}

TEST_CASE("a string that fits exactly does not exhaust the budget",
          "[waf_input_budget]") {
  WafInputBudget budget{7};
  auto kept = budget.charge_string("abc"sv, true);
  REQUIRE(kept);
  CHECK(*kept == "abc");
  CHECK_FALSE(budget.exhausted());
}

TEST_CASE("truncation keeps whole UTF-8 characters", "[waf_input_budget]") {
  // the 2-byte "é" crosses the limit, so it is dropped entirely
  WafInputBudget budget{4};
  auto kept = budget.charge_string("a\xc3\xa9"sv, false);
  REQUIRE(kept);
  CHECK(*kept == "a");
}
