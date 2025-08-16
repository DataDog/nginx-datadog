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

namespace {
// private type
template <std::uint32_t RefreshesPerMin>
struct SharedLimiterState {
  std::atomic<std::uint32_t> tokens{0};
  std::atomic<std::int64_t> last_refresh_us{0};
  std::uint32_t max_per_min{0};
  std::array<std::uint32_t, RefreshesPerMin> refresh_amounts{};
};
}  // namespace

template <std::uint32_t RefreshesPerMin>
class SharedLimiter {
    static_assert(RefreshesPerMin > 0,
                  "refreshes_per_min must be greater than 0");
    static_assert(1000000 % RefreshesPerMin == 0,
                  "refreshes_per_min must be a divisor of 1,000,000");

public:
    using StateType = SharedLimiterState<RefreshesPerMin>;
    
    SharedLimiter(StateType* shared_state) 
        : state_{shared_state} {}

    bool allow() {
        if (!state_) {
            return false;
        }

        refresh();

        std::uint32_t current_tokens =
            state_->tokens.load(std::memory_order_relaxed);
        do {
            if (current_tokens == 0) {
              return false;
            }
        } while (!state_->tokens.compare_exchange_weak(
            current_tokens, current_tokens - 1, std::memory_order_relaxed,
            std::memory_order_relaxed));

        return true;
    }

    static void initialize_shared_state(StateType& state, std::uint32_t max_per_min) {
        state.tokens.store(max_per_min, std::memory_order_relaxed);

        state.last_refresh_us.store(rounded_now_us(),
                                     std::memory_order_relaxed);

        state.max_per_min = max_per_min;
        
        distribute_refresh_amounts(state, max_per_min);
    }

private:
    StateType* state_;
    
    static constexpr auto kRefreshPeriodUs = 
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::minutes(1)).count() / RefreshesPerMin;

    static std::int64_t rounded_now_us() {
        auto now = std::chrono::steady_clock::now();
        auto epoch_us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
        return round_down_microseconds(epoch_us);
    }

    void refresh() {
        if (!state_) {
            return;
        }
        
        std::int64_t rounded_us = rounded_now_us();
        std::int64_t last_refresh = state_->last_refresh_us.load(std::memory_order_relaxed);
        
        if (rounded_us == last_refresh) {
            return;
        }
        
        if (!state_->last_refresh_us.compare_exchange_strong(last_refresh, rounded_us, 
                                                             std::memory_order_relaxed, 
                                                             std::memory_order_relaxed)) {
            // another thread is updating, let it handle the refresh
            return;
        }
        
        // how many refresh periods have passed
        std::uint32_t extra_tokens = 0;
        auto periods_passed = (rounded_us - last_refresh) / kRefreshPeriodUs;
        
        for (std::uint32_t i = 1; i <= periods_passed && extra_tokens < state_->max_per_min; i++) {
            auto period_index = ((last_refresh / kRefreshPeriodUs) + i) % RefreshesPerMin;
            extra_tokens += state_->refresh_amounts[period_index];
        }
        
        if (extra_tokens > 0) {
            // atomically add tokens, capping at max_per_min
            std::uint32_t current_tokens = state_->tokens.load(std::memory_order_relaxed);
            std::uint32_t new_tokens;
            do {
                new_tokens = current_tokens + extra_tokens;
                if (new_tokens > state_->max_per_min) {
                    new_tokens = state_->max_per_min;
                }
            } while (!state_->tokens.compare_exchange_weak(current_tokens, new_tokens, 
                                                           std::memory_order_relaxed, 
                                                           std::memory_order_relaxed));
        }
    }
    
    static std::int64_t round_down_microseconds(std::int64_t us) {
        return (us / kRefreshPeriodUs) * kRefreshPeriodUs;
    }
    
    static void distribute_refresh_amounts(StateType& state, std::uint32_t max_per_min) {
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
    using StateType = SharedLimiterState<RefreshesPerMin>;

   public:
    static ngx_shm_zone_t* create_zone(ngx_conf_t& cf, std::string_view zone_name) {
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

    static std::optional<SharedLimiter<RefreshesPerMin>> get_limiter(ngx_shm_zone_t* shm_zone, std::uint32_t max_per_min) {
        if (shm_zone == nullptr || shm_zone->data == nullptr ||
            max_per_min == 0) {
            return std::nullopt;
        }

        StateType *state = static_cast<StateType*>(shm_zone->data);
        SharedLimiter<RefreshesPerMin> limiter{state};

        auto* shpool = reinterpret_cast<ngx_slab_pool_t*>(shm_zone->shm.addr);
        ngx_shmtx_lock(&shpool->mutex);
        if (state->max_per_min == 0) {
            SharedLimiter<RefreshesPerMin>::initialize_shared_state(
                *state, max_per_min);
        }
        ngx_shmtx_unlock(&shpool->mutex);

        return {limiter};
    }

 private:
    inline static constexpr size_t kZoneSize = sizeof(SharedLimiterState<RefreshesPerMin>);

    static inline ngx_int_t shared_limiter_zone_init(ngx_shm_zone_t* shm_zone, void*data) {
        StateType *old_state = static_cast<StateType*>(data);

        if (old_state != nullptr) {
            ngx_log_error(NGX_LOG_INFO, shm_zone->shm.log, 0,
                          "Reusing existing shared memory for rate limiter");
            shm_zone->data = old_state;
            new (old_state) StateType{};
            return NGX_OK;
        }

        // Initialize new shared memory
        auto *pool = reinterpret_cast<ngx_slab_pool_t *>(shm_zone->shm.addr);
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
