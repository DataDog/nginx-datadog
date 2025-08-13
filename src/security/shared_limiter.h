#pragma once

extern "C" {
#include <ngx_core.h>
}

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

#include "string_util.h"

namespace datadog::nginx::security {

namespace internal {
struct TokensAndRefresh {
  std::uint32_t tokens;
  std::uint32_t last_refresh;
};

static_assert(std::atomic<TokensAndRefresh>::is_always_lock_free,
              "TokensAndRefresh must be lock-free");

template <std::uint32_t RefreshesPerMin>
struct SharedLimiterState {
  std::atomic<TokensAndRefresh> tokens_and_refresh{};
  std::uint32_t max_per_min{};
  std::array<std::uint32_t, RefreshesPerMin> refresh_amounts{};
};
}  // namespace internal

template <std::uint32_t RefreshesPerMin,
          typename Clock = std::chrono::steady_clock>
class SharedLimiter {
  static_assert(RefreshesPerMin > 0,
                "refreshes_per_min must be greater than 0");
  static_assert(1000000 % RefreshesPerMin == 0,
                "refreshes_per_min must be a divisor of 1,000,000");

 public:
  using StateType = internal::SharedLimiterState<RefreshesPerMin>;

  SharedLimiter(StateType* shared_state) : state_{shared_state} {}

  bool allow() {
    if (!state_) {
      return false;
    }

    internal::TokensAndRefresh current = refresh();

    internal::TokensAndRefresh new_val;
    do {
      if (current.tokens == 0) {
        return false;
      }
      new_val = {.tokens = current.tokens - 1,
                 .last_refresh = current.last_refresh};
    } while (!state_->tokens_and_refresh.compare_exchange_weak(
        current, new_val, std::memory_order_relaxed,
        std::memory_order_relaxed));

    return true;
  }

  static void initialize_shared_state(StateType& state,
                                      std::uint32_t max_per_min) {
    internal::TokensAndRefresh initial = {.tokens = max_per_min,
                                          .last_refresh = now_tick()};
    state.tokens_and_refresh.store(initial, std::memory_order_relaxed);

    state.max_per_min = max_per_min;

    distribute_refresh_amounts(state, max_per_min);
  }

 private:
  StateType* state_;

  static constexpr std::uint32_t kRefreshPeriodUs =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::minutes(1))
          .count() /
      RefreshesPerMin;

  static std::uint32_t now_tick() {
    auto now = Clock::now();
    auto epoch_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        now.time_since_epoch())
                        .count();
    return static_cast<std::uint32_t>(epoch_us / kRefreshPeriodUs);
  }

  internal::TokensAndRefresh refresh() {
    if (!state_) {
      return {0, 0};
    }

    std::uint32_t tick = now_tick();
    internal::TokensAndRefresh current =
        state_->tokens_and_refresh.load(std::memory_order_relaxed);

    while (tick > current.last_refresh) {
      // how many refresh periods have passed
      std::uint32_t extra_tokens = 0;
      std::uint32_t periods_passed = (tick - current.last_refresh);

      for (std::uint32_t i = 1;
           i <= periods_passed && extra_tokens < state_->max_per_min; i++) {
        auto period_index = (current.last_refresh + i) % RefreshesPerMin;
        extra_tokens += state_->refresh_amounts[period_index];
      }

      internal::TokensAndRefresh new_val = {current.tokens + extra_tokens,
                                            tick};
      if (new_val.tokens > state_->max_per_min) {
        new_val.tokens = state_->max_per_min;
      }

      if (state_->tokens_and_refresh.compare_exchange_weak(
              current, new_val, std::memory_order_relaxed,
              std::memory_order_relaxed)) {
        return new_val;
      }
      // CAS failed, retry with updated current value
    }

    return current;
  }

  static void distribute_refresh_amounts(StateType& state,
                                         std::uint32_t max_per_min) {
    std::uint32_t base = max_per_min / RefreshesPerMin;
    std::uint32_t remainder = max_per_min % RefreshesPerMin;

    // initialize all to base amount
    state.refresh_amounts.fill(base);

    // distribute remainder
    for (std::uint32_t i = 0; i < remainder; i++) {
      std::uint32_t index = (i * RefreshesPerMin) / remainder;
      state.refresh_amounts[index]++;
    }
  }
};

template <std::uint32_t RefreshesPerMin>
class SharedLimiterZoneManager {
  using StateType = internal::SharedLimiterState<RefreshesPerMin>;

 public:
  static ngx_shm_zone_t* create_zone(ngx_conf_t& cf,
                                     std::string_view zone_name) {
    static constexpr uintptr_t zone_tag = 0xD47AD06;
    ngx_str_t name = to_ngx_str(zone_name);

    // create or get existing shared memory zone
    ngx_shm_zone_t* shm_zone = ngx_shared_memory_add(
        &cf, &name, 8192, reinterpret_cast<void*>(zone_tag));
    if (shm_zone == nullptr) {
      return nullptr;
    }

    shm_zone->init = shared_limiter_zone_init;

    return shm_zone;
  }

  static std::optional<SharedLimiter<RefreshesPerMin>> get_limiter(
      ngx_shm_zone_t* shm_zone, std::uint32_t max_per_min) {
    if (shm_zone == nullptr || shm_zone->data == nullptr || max_per_min == 0) {
      return std::nullopt;
    }

    StateType* state = static_cast<StateType*>(shm_zone->data);
    SharedLimiter<RefreshesPerMin> limiter{state};

    auto* shpool = reinterpret_cast<ngx_slab_pool_t*>(shm_zone->shm.addr);
    ngx_shmtx_lock(&shpool->mutex);
    if (state->max_per_min == 0) {
      ngx_log_error(
          NGX_LOG_INFO, shm_zone->shm.log, 0,
          "Initializing shared memory for rate limiter on this worker");
      SharedLimiter<RefreshesPerMin>::initialize_shared_state(*state,
                                                              max_per_min);
    }
    ngx_shmtx_unlock(&shpool->mutex);

    return {limiter};
  }

 private:
  inline static constexpr size_t kZoneSize =
      sizeof(internal::SharedLimiterState<RefreshesPerMin>);

  static ngx_int_t shared_limiter_zone_init(ngx_shm_zone_t* shm_zone,
                                            void* data) {
    StateType* old_state = static_cast<StateType*>(data);

    if (old_state != nullptr) {
      ngx_log_error(NGX_LOG_INFO, shm_zone->shm.log, 0,
                    "Reusing existing shared memory for rate limiter");
      shm_zone->data = old_state;
      new (old_state) StateType{};
      return NGX_OK;
    }

    // Initialize new shared memory
    auto* pool = reinterpret_cast<ngx_slab_pool_t*>(shm_zone->shm.addr);
    shm_zone->data = ngx_slab_alloc(pool, sizeof(StateType));
    if (shm_zone->data == nullptr) {
      ngx_log_error(NGX_LOG_EMERG, shm_zone->shm.log, 0,
                    "Failed to allocate shared memory for rate limiter");
      return NGX_ERROR;
    }

    new (shm_zone->data) StateType{};

    return NGX_OK;
  }
};

}  // namespace datadog::nginx::security
