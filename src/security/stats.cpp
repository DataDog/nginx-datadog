#include "stats.h"

#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <netdb.h>
#include <ngx_cycle.h>
#include <sys/socket.h>
#include <unistd.h>
}

using namespace std::literals::string_view_literals;

namespace {

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
void report_memory_stats(std::string_view pid, Send&& send) {
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
                                std::uint64_t val) noexcept {
  // Maximum size for a DogStatsD metric with tags:
  // metric_name:value|type|#tag_key:tag_value
  // The largest uint64_t value is 18446744073709551615 (20 digits)
  static constexpr size_t max_payload_len =
      sizeof(
          "appsec.tasks_submission_failed:18446744073709551615|c|#pid:65535") -
      1;

  static std::array<char, max_payload_len> buf{};

  char* it = buf.begin();
  char* end = buf.end();

  if (it + metric.size() >= end) {
    assert(false && "Buffer overflow: metric name too long");
    return {};
  }
  it = std::copy(metric.begin(), metric.end(), it);

  if (it >= end) {
    assert(false && "Buffer overflow: no space for colon");
    return {};
  }
  *it++ = ':';

  if (it + sizeof("18446744073709551615") - 1 >= end) {
    assert(false && "Buffer overflow: no space for value");
    return {};
  }
  auto [ptr, ec] =
      std::to_chars(it, it + sizeof("18446744073709551615") - 1, val);
  assert(ec == decltype(ec){});
  it = ptr;

  if (it + 2 >= end) {
    assert(false && "Buffer overflow: no space for metric type");
    return {};
  }
  *it++ = '|';
  *it++ = type;

  // Add PID as a tag: |#pid:value
  if (it + sizeof("|#pid:") - 1 + pid.size() >= end) {
    assert(false && "Buffer overflow: no space for PID tag");
    return {};
  }
  *it++ = '|';
  *it++ = '#';
  *it++ = 'p';
  *it++ = 'i';
  *it++ = 'd';
  *it++ = ':';
  it = std::copy(pid.begin(), pid.end(), it);

  size_t len = it - buf.begin();

  return {buf.data(), len};
}

}  // namespace

namespace datadog::nginx::security {

// MetricSender class that owns the UDP socket and handles metric transmission
class MetricSender {
 public:
  static std::unique_ptr<MetricSender> create(std::string_view host,
                                              uint16_t port) {
    struct sockaddr_storage server_address;
    int socket_fd = create_udp_socket(host, port, server_address);
    if (socket_fd == -1) {
      return nullptr;
    }

    const auto pid = std::to_string(getpid());

    // Use private constructor since we're in the same class
    return std::unique_ptr<MetricSender>(
        new MetricSender(socket_fd, server_address, pid));
  }

  ~MetricSender() {
    if (socket_fd_ != -1) {
      close(socket_fd_);
    }
  }

  MetricSender(const MetricSender&) = delete;
  MetricSender& operator=(const MetricSender&) = delete;

  MetricSender(MetricSender&& other) noexcept
      : socket_fd_(std::exchange(other.socket_fd_, -1)),
        server_address_(other.server_address_),
        pid_(std::move(other.pid_)) {}

  MetricSender& operator=(MetricSender&& other) noexcept {
    if (this != &other) {
      if (socket_fd_ != -1) {
        close(socket_fd_);
      }
      socket_fd_ = std::exchange(other.socket_fd_, -1);
      server_address_ = other.server_address_;
      pid_ = std::move(other.pid_);
    }
    return *this;
  }

  void send_metric(std::string_view metric_name, char metric_type,
                   std::uint64_t val) noexcept {
    std::string_view data = prepare_metric(metric_name, pid_, metric_type, val);

    auto res =
        sendto(socket_fd_, data.data(), data.length(), 0,
               reinterpret_cast<const struct sockaddr*>(&server_address_),
               sizeof(server_address_));
    if (res == -1) {
      ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                    "Stats: failed to send metric: %s", strerror(errno));
    }
    if (res != static_cast<ssize_t>(data.length())) {
      ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                    "Stats: incomplete write for metric %.*s: %zd != %zu",
                    static_cast<int>(metric_name.length()), metric_name.data(),
                    res, data.length());
    }
  }

  bool is_valid() const { return socket_fd_ != -1; }

 private:
  MetricSender(int socket_fd, const struct sockaddr_storage& server_address,
               std::string_view pid)
      : socket_fd_{socket_fd}, server_address_{server_address}, pid_{pid} {}

  static int create_udp_socket(std::string_view host, uint16_t port,
                               struct sockaddr_storage& server_address) {
    // Ensure host is null-terminated for getaddrinfo
    std::string host_str{host};
    std::string port_str = std::to_string(port);

    struct addrinfo hints = {
        .ai_flags = AI_ALL,
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
    };

    struct addrinfo* result;
    if (getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &result) != 0) {
      return -1;
    }

    int sockfd = -1;
    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
      sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sockfd == -1) {
        continue;
      }

      memcpy(&server_address, rp->ai_addr, rp->ai_addrlen);
      break;  // Use first successful socket
    }

    freeaddrinfo(result);
    return sockfd;
  }

  int socket_fd_;
  struct sockaddr_storage server_address_;
  std::string pid_;
};

Stats& Stats::instance() {
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

  auto sender = MetricSender::create(host, port);
  if (!sender) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "Stats: failed to create MetricSender");
    return false;
  }

  reporting_thread_ =
      std::thread(&Stats::reporting_loop, this, std::move(sender));
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

void Stats::reporting_loop(std::unique_ptr<MetricSender> sender) {
  ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                "Stats: reporting loop started");

#ifdef __APPLE__
  pthread_setname_np("appsec-stats");
#else
  pthread_setname_np(pthread_self(), "appsec-stats");
#endif

  if (!sender || !sender->is_valid()) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "Stats: MetricSender is invalid");
    return;
  }

  const auto pid = std::to_string(getpid());

  std::unique_lock<std::mutex> lock(stop_mutex_);

  while (true) {
    const auto contexts_started = contexts_started_.load();
    const auto contexts_closed = contexts_closed_.load();
    const auto tasks_created = tasks_created_.load();
    const auto tasks_submitted = tasks_submitted_.load();
    const auto tasks_completed = tasks_completed_.load();
    const auto tasks_submission_failed = tasks_submission_failed_.load();
    const auto tasks_destructed = tasks_destructed_.load();

    sender->send_metric("appsec.contexts_started", 'c', contexts_started);
    sender->send_metric("appsec.contexts_closed", 'c', contexts_closed);
    sender->send_metric("appsec.tasks_created", 'c', tasks_created);
    sender->send_metric("appsec.tasks_submitted", 'c', tasks_submitted);
    sender->send_metric("appsec.tasks_completed", 'c', tasks_completed);
    sender->send_metric("appsec.tasks_submission_failed", 'c',
                        tasks_submission_failed);
    sender->send_metric("appsec.tasks_destructed", 'c', tasks_destructed);

    report_memory_stats(pid, [&sender](std::string_view metric_name,
                                       char metric_type, auto val) {
      sender->send_metric(metric_name, metric_type, val);
    });

    stop_cv_.wait_until(
        lock, std::chrono::steady_clock::now() + std::chrono::seconds(10),
        [this] { return stop_flag_; });
    if (stop_flag_) {
      break;
    }
  }
}

}  // namespace datadog::nginx::security
