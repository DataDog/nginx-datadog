#include "ngx_logger.h"

#include <sstream>

#include "string_util.h"

extern "C" {
#include <ngx_core.h>
#include <ngx_log.h>
}

namespace datadog::nginx {

void NgxLogger::log_error(const LogFunc& write) {
  std::ostringstream stream;
  write(stream);
  log_error(stream.str());
}

void NgxLogger::log_startup(const LogFunc& write) {
  std::ostringstream stream;
  write(stream);
  std::string message = stream.str();

  std::lock_guard<std::mutex> lock(mutex_);
  ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, message.data());
}

void NgxLogger::log_error(const dd::Error& error) {
  const ngx_str_t ngx_message = to_ngx_str(error.message);

  std::lock_guard<std::mutex> lock(mutex_);
  ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                "nginx-datadog: [error code %d] %V", int(error.code),
                &ngx_message);
}

void NgxLogger::log_error(std::string_view message) {
  const ngx_str_t ngx_message = to_ngx_str(message);

  std::lock_guard<std::mutex> lock(mutex_);
  ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "nginx-datadog: %V",
                &ngx_message);
}

void NgxLogger::log_debug(const LogFunc& write) {
#if NGX_DEBUG
  if (!(ngx_cycle->log->log_level & NGX_LOG_DEBUG_HTTP)) {
    return;
  }
  std::ostringstream stream;
  write(stream);
  log_debug(stream.str());
#endif
}

void NgxLogger::log_debug(std::string_view message) {
#if NGX_DEBUG
  if (!(ngx_cycle->log->log_level & NGX_LOG_DEBUG_HTTP)) {
    return;
  }
  const ngx_str_t ngx_message = to_ngx_str(message);

  std::lock_guard<std::mutex> lock(mutex_);
  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, "datadog: %V",
                 &ngx_message);
#endif
}

}  // namespace datadog::nginx
