/*
 * Unless explicitly stated otherwise all files in this repository are licensed
 * under the Apache 2.0 License. This product includes software developed at
 * Datadog (https://www.datadoghq.com/).
 *
 * Copyright 2024-Present Datadog, Inc.
 */

#include <utility>

// This component provides a class template, `CleanupFuncGuard`, together with
// a utility function, `defer`, that allow for deferred execution of arbitrary
// code at the end of a lexical scope.
//
//     const auto guard = defer([]() {
//         /* this is executed when `guard` is destroyed */
//     });
//
// For example, the following code restores the value of `conf.cmd` anywhere
// the enclosing function returns or throws an exception ("!"), but only if
// `guard` has been initialized ("←").
//
//     int inject_config(Config* conf, const char* old_name, const char*
//     new_name) try {
//         if (std::strcmp(old_name, new_name) == 0) {
//             return 0;
//         }
//
//         const auto *const old_command = conf.cmd;
//         std::vector<const char*> new_command = *old_command;
//         new_command[0] = new_name;
//         complicated_initialization(&new_command);
//
//         conf.cmd = &new_command;
//         const auto guard = defer([&]() { conf.cmd = old_command; }); // ←
//
//         if (const int rc = dispatch_config(conf) /* ! */) {
//             log_error(rc.message());
//             return rc.code(); // !
//         }
//
//         /* more code here ... */ // !
//
//         return 0; // !
//     } catch (const LibraryError& error) {
//         log_error(error.what());
//         return error.code(); // !
//     }
//
// The function template `defer` returns an object that, when destroyed,
// invokes the function-like object passed to `defer`.

namespace datadog {
namespace rum {

// This class template invokes a specified function-like object in its
// destructor.  Moving from a `CleanupFuncGuard` disables this behavior, and
// the type is move-only, so the function-like object will not be invoked more
// than once.
template <typename Func> class CleanupFuncGuard {
  Func on_destroy_;
  bool active_; // whether to call `on_destroy_` in the destructor

public:
  explicit CleanupFuncGuard(Func &&func)
      : on_destroy_(std::move(func)), active_(true) {}

  CleanupFuncGuard(CleanupFuncGuard &&other)
      : on_destroy_(std::move(other.on_destroy_)), active_(true) {
    other.active_ = false;
  }

  CleanupFuncGuard(const CleanupFuncGuard &) = delete;

  ~CleanupFuncGuard() {
    if (active_) {
      on_destroy_();
    }
  }
};

// Return a guard object that invokes the specified `func` when destroyed.
// Intended usage:
//
//     const auto guard = defer(/* ... lambda expression ... */);
template <typename Func> CleanupFuncGuard<Func> defer(Func &&func) {
  return CleanupFuncGuard<Func>(std::forward<Func>(func));
}

} // namespace rum
} // namespace datadog
