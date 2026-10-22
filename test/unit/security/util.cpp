#include "security/util.h"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

namespace dnsec = datadog::nginx::security;

TEST_CASE("LowercaseStringView checks literals at compile time", "[util]") {
  static constexpr dnsec::LowercaseStringView lowercase{"content-type"};

  CHECK(lowercase == "content-type");
}

TEST_CASE("LowercaseStringView checks runtime values", "[util]") {
  using RuntimeCheck = dnsec::LowercaseStringView::WithRuntimeCheckTag;

  std::string lowercase = "content-type";
  CHECK_NOTHROW((dnsec::LowercaseStringView{lowercase, RuntimeCheck{}}));

  std::string mixed_case = "Content-Type";
  CHECK_THROWS_AS((dnsec::LowercaseStringView{mixed_case, RuntimeCheck{}}),
                  std::invalid_argument);
}
