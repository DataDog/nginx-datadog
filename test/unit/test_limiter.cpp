#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "security/shared_limiter.h"

namespace dnsec = datadog::nginx::security;

// Mock clock for testing time-dependent behavior
class MockClock {
public:
    using duration = std::chrono::steady_clock::duration;
    using rep = std::chrono::steady_clock::rep;
    using period = std::chrono::steady_clock::period;
    using time_point = std::chrono::time_point<MockClock, duration>;
    static constexpr bool is_steady = true;

    static time_point now() noexcept {
        return current_time_;
    }

    static void set_time(time_point time) {
        current_time_ = time;
    }

    static void advance_time(duration d) {
        current_time_ += d;
    }

    static void reset() {
        current_time_ = time_point{};
    }

private:
    static time_point current_time_;
};

MockClock::time_point MockClock::current_time_ = MockClock::time_point{};

// Helper function to create and initialize a SharedLimiter for testing
template<std::uint32_t RefreshesPerMin>
auto create_test_limiter(std::uint32_t max_per_min) {
    using StateType = dnsec::internal::SharedLimiterState<RefreshesPerMin>;
    static thread_local StateType state{};
    dnsec::SharedLimiter<RefreshesPerMin, MockClock>::initialize_shared_state(state, max_per_min);
    return dnsec::SharedLimiter<RefreshesPerMin, MockClock>{&state};
}

TEST_CASE("Limiter basic functionality", "[limiter]") {
    MockClock::reset();
    
    SECTION("Constructor initializes with correct token count") {
        auto limiter = create_test_limiter<100>(100);
        
        // Should start with max tokens available
        for (int i = 0; i < 100; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        
        // Should be exhausted after max tokens
        REQUIRE(limiter.allow() == false);
    }
    
    SECTION("Zero max tokens limiter") {
        auto limiter = create_test_limiter<100>(0);
        
        // Should never allow requests
        REQUIRE(limiter.allow() == false);
        REQUIRE(limiter.allow() == false);
    }
    
    SECTION("Single token limiter") {
        auto limiter = create_test_limiter<100>(1);
        
        // Should allow one request then deny
        REQUIRE(limiter.allow() == true);
        REQUIRE(limiter.allow() == false);
        REQUIRE(limiter.allow() == false);
    }
}

TEST_CASE("Limiter token refresh behavior", "[limiter]") {
    MockClock::reset();
    
    SECTION("Tokens refresh after time passes") {
        auto limiter = create_test_limiter<100>(100); // 100 tokens per minute, 100 refreshes per minute = 1 token per 0.6 seconds
        
        // Exhaust all tokens
        for (int i = 0; i < 100; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
        
        // Advance time by 600ms (should add 1 token with 100 refreshes per minute)
        MockClock::advance_time(std::chrono::milliseconds(600));
        REQUIRE(limiter.allow() == true);
        REQUIRE(limiter.allow() == false);
        
        // Advance time by 3.5 seconds (should add 5 tokens)
        MockClock::advance_time(std::chrono::milliseconds(3500));
        for (int i = 0; i < 5; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
    }
    
    SECTION("Tokens don't exceed maximum") {
        auto limiter = create_test_limiter<10>(10);
        
        for (int i = 0; i < 10; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
        
        // Advance time by two minutes (should refill completely but not exceed max)
        MockClock::advance_time(std::chrono::minutes(2));
        
        // Should have exactly 10 tokens, not more
        for (int i = 0; i < 10; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
    }
    
    SECTION("Partial token consumption and refresh") {
        auto limiter = create_test_limiter<100>(100);
        
        // Use half the tokens
        for (int i = 0; i < 50; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        
        // Advance time by 6 seconds (should add 10 tokens with 100 refreshes per minute)
        MockClock::advance_time(std::chrono::seconds(6));
        
        // Should have 50 + 10 = 60 tokens available
        for (int i = 0; i < 60; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
    }
}

TEST_CASE("Limiter refresh amount distribution", "[limiter]") {
    MockClock::reset();
    
    SECTION("Even distribution when max_per_min is divisible by refreshes_per_min") {
        auto limiter = create_test_limiter<10>(100); // 100 tokens per minute, 10 refreshes = 10 tokens per refresh
        
        // Exhaust tokens
        for (int i = 0; i < 100; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
        
        // Each refresh period should add exactly 10 tokens
        for (int refresh = 0; refresh < 10; ++refresh) {
            MockClock::advance_time(std::chrono::seconds(6)); // 60s / 10 refreshes = 6s per refresh
            for (int token = 0; token < 10; ++token) {
                REQUIRE(limiter.allow() == true);
            }
            REQUIRE(limiter.allow() == false);
        }
    }
    
    SECTION("Uneven distribution when max_per_min is not divisible by refreshes_per_min") {
        auto limiter = create_test_limiter<10>(107); // 107 tokens per minute, 10 refreshes
        // Should distribute as: 7 refreshes get 10 tokens, 3 refreshes get 11 tokens
        
        // Exhaust tokens
        for (int i = 0; i < 107; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
        
        // Track total tokens refreshed
        int total_refreshed = 0;
        for (int refresh = 0; refresh < 10; ++refresh) {
            MockClock::advance_time(std::chrono::seconds(6)); // 60s / 10 refreshes = 6s per refresh
            
            int tokens_this_refresh = 0;
            while (limiter.allow()) {
                tokens_this_refresh++;
            }
            
            // Should be either 10 or 11 tokens per refresh
            REQUIRE((tokens_this_refresh == 10 || tokens_this_refresh == 11));
            total_refreshed += tokens_this_refresh;
        }
        
        // Total should equal original max
        REQUIRE(total_refreshed == 107);
    }
}

TEST_CASE("Limiter edge cases and boundary conditions", "[limiter]") {
    MockClock::reset();
    
    SECTION("Time doesn't advance") {
        auto limiter = create_test_limiter<10>(10);
        
        // Exhaust tokens
        for (int i = 0; i < 10; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
        
        // Multiple calls without time advancement should continue to deny
        for (int i = 0; i < 100; ++i) {
            REQUIRE(limiter.allow() == false);
        }
    }
    
    SECTION("Large time jump") {
        auto limiter = create_test_limiter<100>(100);
        
        // Exhaust tokens
        for (int i = 0; i < 100; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
        
        // Jump forward by 10 minutes
        MockClock::advance_time(std::chrono::hours(1000));
        
        // Should have full bucket available (capped at max)
        for (int i = 0; i < 100; ++i) {
            REQUIRE(limiter.allow() == true);
        }
        REQUIRE(limiter.allow() == false);
    }
    
    SECTION("Very small max_per_min with high refresh rate") {
        auto limiter = create_test_limiter<1000>(1);
        
        // Should start with 1 token
        REQUIRE(limiter.allow() == true);
        REQUIRE(limiter.allow() == false);
        
        // Should get 1 token per minute regardless of refresh rate
        MockClock::advance_time(std::chrono::minutes(1));
        REQUIRE(limiter.allow() == true);
        REQUIRE(limiter.allow() == false);
    }
}
