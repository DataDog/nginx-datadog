#pragma once

// adapted from: https://en.cppreference.com/w/cpp/language/coroutines
#include <coroutine>
#include <exception>
#include <stdexcept>

namespace datadog::nginx::security {

template <typename T>
struct Generator {
  struct promise_type;  // NOLINT(readability-identifier-naming)
  using HandleType = std::coroutine_handle<promise_type>;

  struct promise_type  // required
  {
    T value_;
    std::exception_ptr exception_;

    Generator get_return_object() {
      return Generator(HandleType::from_promise(*this));
    }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() { exception_ = std::current_exception(); }
    template <std::convertible_to<T> From>
    std::suspend_always yield_value(From&& from) {
      value_ = std::forward<From>(from);  // caching the result in promise
      return {};
    }
    void return_void() {}
  };

  Generator(HandleType h) : h_(h) {}
  ~Generator() { h_.destroy(); }
  bool has_next() {
    fill();
    return !h_.done();
  }
  T peek() {
    fill();
    return h_.promise().value_;
  }
  T next() {
    fill();
    fetch_next_ = true;
    return std::move(h_.promise().value_);
  }

 private:
  HandleType h_;
  bool fetch_next_ = true;

  void fill() {
    if (!fetch_next_) {
      return;
    }

    if (h_.done()) {
      throw std::runtime_error{"fetch from a done generator"};
    }

    h_();
    if (h_.promise().exception_) {
      std::rethrow_exception(h_.promise().exception_);
    }
    // propagate coroutine exception in called context

    fetch_next_ = false;
  }
};

}  // namespace datadog::nginx::security
