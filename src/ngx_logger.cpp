#include "ngx_logger.h"
#include "string_util.h"

#include <sstream>

extern "C" {
#include <ngx_core.h>
#include <ngx_log.h>
}

namespace datadog {
namespace nginx {

void NgxLogger::log_error(const dd::Logger::LogFunc& write) {
  std::ostringstream stream;
  write(stream);
  log_error(stream.str());
}

void NgxLogger::log_startup(const dd::Logger::LogFunc& write) {
  std::ostringstream stream;
  write(stream);
  std::string message = stream.str();

  std::lock_guard<std::mutex> lock(mutex_);
  (void)ngx_write_fd(ngx_stderr, message.data(), message.size());
}

void NgxLogger::log_error(const dd::Error& error) {
  const ngx_str_t ngx_message = to_ngx_str(error.message);

  std::lock_guard<std::mutex> lock(mutex_);
  ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "datadog: [error code %d] %V",
                int(error.code), &ngx_message);
}

void NgxLogger::log_error(std::string_view message) {
  const ngx_str_t ngx_message = to_ngx_str(message);

  std::lock_guard<std::mutex> lock(mutex_);
  ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "datadog: %V", &ngx_message);
}

}  // namespace nginx
}  // namespace datadog
