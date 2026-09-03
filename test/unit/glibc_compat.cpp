#include <catch2/catch_test_macros.hpp>
#include <cfenv>
#include <cmath>
#include <limits>

namespace {

double compat_round(double value) {
  double (*volatile function)(double) = ::round;
  return function(value);
}

class RoundingModeGuard {
  int original_;

 public:
  RoundingModeGuard() : original_(std::fegetround()) {
    REQUIRE(original_ != -1);
  }

  ~RoundingModeGuard() { std::fesetround(original_); }
};

}

TEST_CASE("musl round compatibility", "[glibc_compat]") {
  SECTION("rounds halfway away from zero") {
    CHECK(compat_round(0.5) == 1.0);
    CHECK(compat_round(1.5) == 2.0);
    CHECK(compat_round(-0.5) == -1.0);
    CHECK(compat_round(-1.5) == -2.0);
  }

  SECTION("handles adjacent halfway values") {
    CHECK(compat_round(std::nextafter(0.5, 0.0)) == 0.0);
    CHECK(compat_round(std::nextafter(0.5, 1.0)) == 1.0);
    CHECK(compat_round(std::nextafter(-0.5, 0.0)) == -0.0);
    CHECK(compat_round(std::nextafter(-0.5, -1.0)) == -1.0);
  }

  SECTION("preserves signed zero") {
    CHECK_FALSE(std::signbit(compat_round(0.0)));
    CHECK(std::signbit(compat_round(-0.0)));
    CHECK_FALSE(std::signbit(compat_round(0.25)));
    CHECK(std::signbit(compat_round(-0.25)));
  }

  SECTION("handles non-finite and large values") {
    const double infinity = std::numeric_limits<double>::infinity();
    const double maximum = std::numeric_limits<double>::max();

    CHECK(compat_round(infinity) == infinity);
    CHECK(compat_round(-infinity) == -infinity);
    CHECK(std::isnan(compat_round(std::numeric_limits<double>::quiet_NaN())));
    CHECK(compat_round(maximum) == maximum);
    CHECK(compat_round(-maximum) == -maximum);
  }

  SECTION("ignores the current rounding mode") {
    RoundingModeGuard guard;
    for (const int mode :
         {FE_DOWNWARD, FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD}) {
      REQUIRE(std::fesetround(mode) == 0);
      CHECK(compat_round(0.5) == 1.0);
      CHECK(compat_round(-0.5) == -1.0);
      CHECK(compat_round(1.4) == 1.0);
      CHECK(compat_round(-1.4) == -1.0);
    }
  }
}
