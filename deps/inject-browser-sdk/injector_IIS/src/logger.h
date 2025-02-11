/*
 * Unless explicitly stated otherwise all files in this repository are licensed
 * under the Apache 2.0 License. This product includes software developed at
 * Datadog (https://www.datadoghq.com/).
 *
 * Copyright 2024-Present Datadog, Inc.
 */

#pragma once
#include "event_provider.h"
#include "framework.h"
#include <datadog/error.h>
#include <datadog/logger.h>
#include <format>
#include <sstream>

namespace datadog::rum {

// Logger using the Event Logging API
// <https://learn.microsoft.com/en-us/windows/win32/eventlog/event-logging>
//
// TODO(@dmehala): Refactor to use the Windows Event Log API
// <https://learn.microsoft.com/en-us/windows/win32/wes/windows-event-log>
class Logger : public datadog::tracing::Logger {
  HANDLE m_eventLog;
  std::ostringstream m_stream;

public:
  bool enable_debug_logs = true;

  Logger() {
    m_eventLog = RegisterEventSource(NULL, L"Datadog-RUM-Instrumentation");
  }

  virtual ~Logger() {
    if (m_eventLog != NULL) {
      DeregisterEventSource(m_eventLog);
      m_eventLog = NULL;
    }
  }

  virtual void info(std::string message) {
    report_event(std::move(message), EVENTLOG_INFORMATION_TYPE,
                 INJECTOR_CATEGORY, MSG_GENERIC_INFO);
  }

  virtual void debug(std::string message) {
    if (!enable_debug_logs)
      return;

    report_event(std::move(message), EVENTLOG_INFORMATION_TYPE,
                 INJECTOR_CATEGORY, MSG_GENERIC_DEBUG);
  }

  virtual void error(std::string message) {
    report_event(std::move(message), EVENTLOG_ERROR_TYPE, INJECTOR_CATEGORY,
                 MSG_GENERIC_ERROR);
  }

  void report_event(std::string message, WORD type, WORD category,
                    DWORD id) const {
    if (m_eventLog == nullptr)
      return;

    LPCSTR szNotification = message.c_str();
    ReportEventA(m_eventLog,      // hEventLog
                 type,            // wType
                 category,        // wCategory
                 id,              // dwEventID
                 NULL,            // lpUserSid
                 1,               // wNumStrings
                 0,               // dwDataSize
                 &szNotification, // lpStrings
                 NULL);           // lpRawData
  }

  // Note(@dmehala): Don't print the tracer startup configuration.
  void log_startup(const LogFunc &) override {}

  void log_error(const LogFunc &f) override {
    m_stream.clear();
    m_stream.str("");

    f(m_stream);
    report_event(m_stream.str(), EVENTLOG_ERROR_TYPE, TRACER_CATEGORY,
                 MSG_GENERIC_ERROR);
  }

  void log_error(const datadog::tracing::Error &error) override {
    report_event(std::format("[dd-trace-cpp error code {}] {}",
                             static_cast<int>(error.code), error.message),
                 EVENTLOG_ERROR_TYPE, TRACER_CATEGORY, MSG_GENERIC_ERROR);
  }

  void log_error(datadog::tracing::StringView sv) override {
    report_event(std::string(sv), EVENTLOG_ERROR_TYPE, TRACER_CATEGORY,
                 MSG_GENERIC_ERROR);
  }
};
} // namespace datadog::rum
