/*
 * Unless explicitly stated otherwise all files in this repository are licensed
 * under the Apache 2.0 License. This product includes software developed at
 * Datadog (https://www.datadoghq.com/).
 *
 * Copyright 2024-Present Datadog, Inc.
 */

#pragma once

#include "framework.h"
#include "injectbrowsersdk.h"
#include "logger.h"

namespace datadog::rum {

// Module identifier retrieve from the module info in register module function.
extern HTTP_MODULE_ID g_module_id;

// Context stored on the application container
struct ModuleContext final : public IHttpStoredContext {
  Snippet *js_snippet = nullptr;
  Logger *logger = nullptr;

  void CleanupStoredContext() override {
    if (js_snippet != nullptr) {
      snippet_cleanup(js_snippet);
    }
  };
};

} // namespace datadog::rum
