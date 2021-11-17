#pragma once

#include <utility>

// TODO: document
namespace datadog {
namespace nginx {

// TODO: document
template <typename Func>
class CleanupFuncGuard {
    Func on_destroy_;
    bool active_;

  public:
    explicit CleanupFuncGuard(Func&& func)
    : on_destroy_(std::move(func))
    , active_(true) {}
    
    CleanupFuncGuard(CleanupFuncGuard&& other)
    : on_destroy_(std::move(other.on_destroy_))
    , active_(true) {
        other.active_ = false;
    }

    CleanupFuncGuard(const CleanupFuncGuard&) = delete;

    ~CleanupFuncGuard() {
        if (active_) {
            on_destroy_();
        }
    }
};

// TODO: document
template <typename Func>
CleanupFuncGuard<Func> defer(Func&& func) {
    return CleanupFuncGuard<Func>(std::forward<Func>(func));
}

} // namespace nginx
} // namespace datadog
