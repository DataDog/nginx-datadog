// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

#include "entrypoint.h"
#include "global_module.h"
#include "http_module.h"
#include "logger.h"
#include "module_context.h"
#include "version.h"
#include <cassert>

__declspec(dllexport) HRESULT
    __stdcall RegisterModule(DWORD server_version,
                             IHttpModuleRegistrationInfo *module_info,
                             IHttpServer *server) {
  assert(server != nullptr);
  assert(module_info != nullptr);

#if defined(DD_INJECTOR_BREAK)
  __debugbreak();
#endif

  auto logger = std::make_shared<datadog::rum::Logger>();
  logger->info(
      std::format("Registering Datadog RUM Injector v{}", FILE_VERSION_STRING));

  datadog::rum::g_module_id = module_info->GetId();

  // NOTE(@dmehala): Keep a pointer on the server for reading configuration
  // when there's a configuration update.
  auto global_module =
      new datadog::rum::GlobalModule(server, server_version, logger);
  if (global_module == nullptr) {
    assert(true);
    logger->error("Failed to register Datadog RUM module to the lack of memory "
                  "to create the global module");
    /*
      If we return an error from the RegisterModule function, IIS will retry
      registering this module, but if it keeps failing then eventually the
      application pool which is calling RegisterModule will come to a complete
      stop. We don't want to cause any downtime on our customers servers, so
      even in the case of a fatal error, we will simply write an error to the
      event log and return S_OK.
      */
    return S_OK;
  }

  module_info->SetGlobalNotifications(
      global_module, datadog::rum::GlobalModule::NotificationsMask);

  auto http_factory = new datadog::rum::HttpModuleFactory;
  if (http_factory == nullptr) {
    assert(true);
    logger->error("Failed to register Datadog RUM module due to the lack of "
                  "memory to create the module factory");
    return S_OK;
  }

  module_info->SetRequestNotifications(
      new datadog::rum::HttpModuleFactory,
      datadog::rum::HttpModule::NotificationsMask, 0);

  return S_OK;
}
