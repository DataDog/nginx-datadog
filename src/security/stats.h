#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <thread>

extern "C" {
#include <sys/socket.h>
}

namespace datadog::nginx::security {

class Stats {
 public:
  static bool start(std::string_view host, uint16_t port) {
    return instance().do_start(host, port);
  }

  static bool stop() { return instance().do_stop(); }

  static void context_started() noexcept {
    instance().contexts_started_.fetch_add(1, std::memory_order_relaxed);
  }

  static void context_closed() noexcept {
    instance().contexts_closed_.fetch_add(1, std::memory_order_relaxed);
  }

  static void task_created() noexcept {
    instance().tasks_created_.fetch_add(1, std::memory_order_relaxed);
  }

  static void task_submitted() noexcept {
    instance().tasks_submitted_.fetch_add(1, std::memory_order_relaxed);
  }

  static void task_submission_failed() noexcept {
    instance().tasks_submission_failed_.fetch_add(1, std::memory_order_relaxed);
  }

  static void task_completed() noexcept {
    instance().tasks_completed_.fetch_add(1, std::memory_order_relaxed);
  }

  static void task_destructed() noexcept {
    instance().tasks_destructed_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  Stats() = default;
  ~Stats();

  Stats(const Stats&) = delete;
  Stats& operator=(const Stats&) = delete;

  static Stats& instance();

  bool do_start(std::string_view host, uint16_t port);
  bool do_stop();

  void reporting_loop();

  int socket_fd_{-1};
  struct sockaddr_storage server_address_ {};

  std::thread reporting_thread_;
  std::mutex stop_mutex_;
  std::condition_variable stop_cv_;
  bool stop_flag_{false};

  std::atomic<std::uint64_t> contexts_started_{0};
  std::atomic<std::uint64_t> contexts_closed_{0};
  std::atomic<std::uint64_t> tasks_created_{0};
  std::atomic<std::uint64_t> tasks_submitted_{0};
  std::atomic<std::uint64_t> tasks_submission_failed_{0};
  std::atomic<std::uint64_t> tasks_completed_{0};
  std::atomic<std::uint64_t> tasks_destructed_{0};
};

}  // namespace datadog::nginx::security
