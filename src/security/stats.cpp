#include "stats.h"

#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include <netdb.h>
#include <ngx_cycle.h>
#include <sys/socket.h>
#include <unistd.h>
}

using namespace std::literals::string_view_literals;

namespace {

int create_udp_socket(const char *hostname, const char *port,
                      struct sockaddr_storage &server_address) {
  struct addrinfo hints = {
      .ai_flags = AI_ALL,
      .ai_family = AF_UNSPEC,
      .ai_socktype = SOCK_DGRAM,
  };

  struct addrinfo *result;
  if (getaddrinfo(hostname, port, &hints, &result) != 0) {
    return -1;
  }

  int sockfd = -1;
  for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
    sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sockfd == -1) {
      continue;
    }

    memcpy(&server_address, rp->ai_addr, rp->ai_addrlen);
  }

  freeaddrinfo(result);

  return sockfd;
}

#ifdef __GLIBC__
#include <malloc.h>
#else
extern "C" struct mallinfo2 {  // NOLINT
  size_t arena;                /* Non-mmapped space allocated (bytes) */
  size_t ordblks;              /* Number of free chunks */
  size_t smblks;               /* Number of free fastbin blocks */
  size_t hblks;                /* Number of mmapped regions */
  size_t hblkhd;               /* Space allocated in mmapped regions (bytes) */
  size_t usmblks;              /* See below */
  size_t fsmblks;              /* Space in freed fastbin blocks (bytes) */
  size_t uordblks;             /* Total allocated space (bytes) */
  size_t fordblks;             /* Total free space (bytes) */
  size_t keepcost;             /* Top-most, releasable space (bytes) */
};
#endif  // __GLIBC__

template <typename Send>
void report_memory_stats(std::string_view pid, Send &&send) {
  static bool checked = false;
  static struct mallinfo2 (*mallinfo2_fn)(void) = nullptr;

  if (!checked) {
    mallinfo2_fn = (struct mallinfo2(*)(void))dlsym(RTLD_DEFAULT, "mallinfo2");
    checked = true;
  }

  if (!mallinfo2_fn) {
    return;  // mallinfo2 not available
  }

  struct mallinfo2 info = mallinfo2_fn();

  std::invoke(std::forward<Send>(send), "memory.arena"sv, 'g', info.arena);
  std::invoke(std::forward<Send>(send), "memory.uordblks"sv, 'g',
              info.uordblks);
  std::invoke(std::forward<Send>(send), "memory.fordblks"sv, 'g',
              info.fordblks);
  std::invoke(std::forward<Send>(send), "memory.hblkhd"sv, 'g', info.hblkhd);
}

std::string_view prepare_metric(const std::string_view metric,
                                std::string_view pid, char type,
                                std::uint32_t val) {
  static constexpr size_t max_payload_len =
      sizeof("appsec.tasks_submission_failed_65535:18446744073709551615|c") - 1;

  static std::array<char, max_payload_len> buf{};

  char *it = std::copy(metric.begin(), metric.end(), buf.begin());
  *it = '_';
  it++;
  it = std::copy(pid.begin(), pid.end(), it);
  *it = ':';
  it++;
  auto [ptr, ec] =
      std::to_chars(it, it + sizeof("18446744073709551615") - 1, val);
  assert(ec == decltype(ec){});
  it = ptr;
  *it = '|';
  it++;
  *it = type;
  it++;

  size_t len = it - buf.begin();

  return {buf.data(), len};
}
}  // namespace

namespace datadog::nginx::security {

Stats &Stats::instance() {
  static Stats instance;
  return instance;
}

Stats::~Stats() {
  if (reporting_thread_.joinable()) {
    stop();
  }
}

bool Stats::do_start(std::string_view host, uint16_t port) {
  if (reporting_thread_.joinable()) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "Stats: already started");
    return false;
  }

  socket_fd_ = create_udp_socket(host.data(), std::to_string(port).c_str(),
                                 server_address_);
  if (socket_fd_ == -1) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "Stats: failed to create socket");
    return false;
  }

  reporting_thread_ = std::thread(&Stats::reporting_loop, this);
  return true;
}

bool Stats::do_stop() {
  if (!reporting_thread_.joinable()) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "Stats: not started");
    return false;
  }

  {
    std::unique_lock<std::mutex> lock(stop_mutex_);
    stop_flag_ = true;
    stop_cv_.notify_all();
  }

  reporting_thread_.join();
  return true;
}

void Stats::reporting_loop() {
  ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                "Stats: reporting loop started");

  const auto pid = std::to_string(getpid());

  pthread_setname_np(pthread_self(), "appsec-stats");

  auto send = [this, &pid](std::string_view metric_name, char metric_type,
                           auto val) {
    std::string_view data = prepare_metric(metric_name, pid, metric_type, val);

    auto res = sendto(socket_fd_, data.data(), data.length(), 0,
                      (const struct sockaddr *)&server_address_,
                      sizeof(server_address_));
    if (res == -1) {
      ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                    "Stats: failed to send metric: %s", strerror(errno));
    }
    if (res != data.length()) {
      ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                    "Stats: incomplete write for metric %.*s: %zd != %zu",
                    static_cast<int>(metric_name.length()), metric_name.data(),
                    res, metric_name.length());
    }
  };

  std::unique_lock<std::mutex> lock(stop_mutex_);

  while (true) {
    std::vector<std::string> metrics;
    const auto contexts_started = contexts_started_.load();
    const auto contexts_closed = contexts_closed_.load();
    const auto tasks_created = tasks_created_.load();
    const auto tasks_submitted = tasks_submitted_.load();
    const auto tasks_completed = tasks_completed_.load();
    const auto tasks_submission_failed = tasks_submission_failed_.load();
    const auto tasks_destructed = tasks_destructed_.load();

    send("appsec.contexts_started", 'c', contexts_started);
    send("appsec.contexts_closed", 'c', contexts_closed);
    send("appsec.tasks_created", 'c', tasks_created);
    send("appsec.tasks_submitted", 'c', tasks_submitted);
    send("appsec.tasks_completed", 'c', tasks_completed);
    send("appsec.tasks_submission_failed", 'c', tasks_submission_failed);
    send("appsec.tasks_destructed", 'c', tasks_destructed);

    report_memory_stats(pid, send);

    stop_cv_.wait_until(
        lock, std::chrono::steady_clock::now() + std::chrono::seconds(10),
        [this] { return stop_flag_; });
    if (stop_flag_) {
      break;
    }
  }

  close(socket_fd_);
}

}  // namespace datadog::nginx::security
