/*
 * Unless explicitly stated otherwise all files in this repository are licensed
 * under the Apache 2.0 License. This product includes software developed at
 * Datadog (https://www.datadoghq.com/).
 *
 * Copyright 2024-Present Datadog, Inc.
 */

#pragma once

#include "framework.h"
#include "logger.h"
#include "module_context.h"

#include <datadog/telemetry/configuration.h>
#include <datadog/telemetry/telemetry.h>
#include <map>

namespace datadog::rum {

class GlobalModule final : public CGlobalModule {
  HTTP_MODULE_ID module_id_;
  IHttpServer *server_;
  std::shared_ptr<Logger> logger_;
  std::unique_ptr<datadog::telemetry::Telemetry> telemetry_;
  std::map<std::wstring, ModuleContext *> configurations_;

public:
  GlobalModule(IHttpServer *server, DWORD server_version,
               std::shared_ptr<Logger> logger);
  ~GlobalModule() = default;
  void Terminate() override;

  static const DWORD NotificationsMask =
      GL_APPLICATION_START | GL_CONFIGURATION_CHANGE | GL_APPLICATION_STOP;

  GLOBAL_NOTIFICATION_STATUS
  OnGlobalApplicationStart(IHttpApplicationStartProvider *provider) override;

  GLOBAL_NOTIFICATION_STATUS
  OnGlobalApplicationStop(IHttpApplicationStopProvider *provider) override;

  GLOBAL_NOTIFICATION_STATUS
  OnGlobalConfigurationChange(
      IGlobalConfigurationChangeProvider *provider) override;
};

} // namespace datadog::rum
