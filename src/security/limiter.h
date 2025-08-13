#include <array>
#include <chrono>
#include <cstdint>

namespace datadog::nginx::security {

template <std::uint32_t RefreshesPerMin,
          typename Clock = std::chrono::steady_clock>
class Limiter {
  static_assert(RefreshesPerMin > 0,
                "refreshes_per_min must be greater than 0");
  static_assert(1000000 % RefreshesPerMin == 0,
                "refreshes_per_min must be a divisor of 1,000,000");

 public:
  Limiter(std::uint32_t max_per_min)
      : max_per_min_{max_per_min}, tokens_{max_per_min}, last_refresh_{round_down(Clock::now())} {
    distribute_refresh_amounts(max_per_min);
  }

  bool allow() {
    refresh();
    if (tokens_ > 0) {
      tokens_--;
      return true;
    }
    return false;
  }

 private:
  void distribute_refresh_amounts(std::uint32_t max_per_min) {
    std::uint32_t base = max_per_min / RefreshesPerMin;
    std::uint32_t remainder = max_per_min % RefreshesPerMin;

    std::fill(refresh_amounts_.begin(), refresh_amounts_.end(), base);

    for (std::uint32_t i = 0; i < remainder; i++) {
      // upper bound: i == remainder - 1 (with remainder > 0)
      // remainder < refreshes_per_min
      // ((remainder - 1) * refreshes_per_min) // remainder < refreshes_per_min
      std::uint32_t index = (i * RefreshesPerMin) / remainder;
      refresh_amounts_[index]++;
    }
  }

  static constexpr std::chrono::time_point<Clock>
  round_down(std::chrono::time_point<Clock> p) noexcept {
    // reduce to last multiple of kRefreshPeriod
    return p - (p.time_since_epoch() % kRefreshPeriod);
  }

  void refresh() {
    const auto now = round_down(Clock::now());

    if (now == last_refresh_) {
      return;
    }

    std::uint32_t extra_tokens = 0;
    for (auto i = last_refresh_.time_since_epoch() / kRefreshPeriod + 1;
         i <= now.time_since_epoch() / kRefreshPeriod &&
         extra_tokens < max_per_min_;
         i++) {
      extra_tokens += refresh_amounts_[i % RefreshesPerMin];
    }

    last_refresh_ = now;

    if (tokens_ + extra_tokens > max_per_min_) {
      tokens_ = max_per_min_;
    } else {
      tokens_ += extra_tokens;
    }
  }

  static constexpr auto kRefreshPeriod =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::minutes(1)) / RefreshesPerMin;

  std::uint32_t max_per_min_;
  std::uint32_t tokens_;
  std::array<std::uint32_t, RefreshesPerMin> refresh_amounts_;
  std::chrono::time_point<Clock> last_refresh_;
};

}  // namespace datadog::nginx::security
