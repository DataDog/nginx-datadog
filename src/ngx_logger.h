#pragma once

#include <datadog/error.h>
#include <datadog/logger.h>

#include <mutex>

#include "dd.h"

namespace datadog {
namespace nginx {

class NgxLogger : public dd::Logger {
  std::mutex mutex_;

  using dd::Logger::LogFunc;

 public:
  void log_error(const LogFunc& write) override;

  void log_startup(const LogFunc& write) override;

  void log_error(const dd::Error& error) override;

  void log_error(std::string_view message) override;

  void log_debug(const LogFunc& write);

  void log_debug(std::string_view message);
};

}  // namespace nginx
}  // namespace datadog
