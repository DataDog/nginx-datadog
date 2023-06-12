#pragma once

#include <datadog/error.h>
#include <datadog/logger.h>

#include <mutex>

#include "dd.h"

namespace datadog {
namespace nginx {

class NgxLogger : public dd::Logger {
  std::mutex mutex_;

 public:
  void log_error(const dd::Logger::LogFunc& write) override;

  void log_startup(const dd::Logger::LogFunc& write) override;

  void log_error(const dd::Error& error) override;

  void log_error(std::string_view message) override;
};

}  // namespace nginx
}  // namespace datadog
