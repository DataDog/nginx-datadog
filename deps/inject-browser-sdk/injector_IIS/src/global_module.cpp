// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

#include "global_module.h"
#include "defer.h"
#include "injectbrowsersdk.h"
#include "module_context.h"
#include "telemetry.h"
#include <cassert>
#include <format>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace datadog::rum {
namespace {

std::string wstring_to_utf8(const std::wstring &wstr) {
  if (wstr.empty())
    return std::string();
  const int size_needed = WideCharToMultiByte(
      CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0],
                      size_needed, nullptr, nullptr);
  return strTo;
}

std::string
make_json_cfg(const int version,
              const std::unordered_map<std::string, std::string> &opts) {
  rapidjson::Document doc;
  doc.SetObject();

  rapidjson::Document::AllocatorType &allocator = doc.GetAllocator();
  doc.AddMember("majorVersion", version, allocator);

  rapidjson::Value rum(rapidjson::kObjectType);
  for (const auto &[key, value] : opts) {
    if (key == "majorVersion") {
      continue;
    } else if (key == "sessionSampleRate" || key == "sessionReplaySampleRate") {
      rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                    rapidjson::Value(std::stod(value)).Move(), allocator);
    } else if (key == "trackResources" || key == "trackLongTasks" ||
               key == "trackUserInteractions") {
      auto b = (value == "true" ? true : false);
      rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                    rapidjson::Value(b).Move(), allocator);
    } else {
      rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                    rapidjson::Value(value.c_str(), allocator).Move(),
                    allocator);
    }
  }

  doc.AddMember("rum", rum, allocator);

  // Convert the document to a JSON string
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  return buffer.GetString();
}

IAppHostProperty *get_property(IAppHostElement *xmlElement,
                               std::wstring_view name) {
  assert(xmlElement);
  IAppHostProperty *property = nullptr;
  if (xmlElement->GetPropertyByName((BSTR)name.data(), &property) != S_OK) {
    return nullptr;
  }

  return property;
}

VARIANT get_property_value(IAppHostProperty &property) {
  VARIANT v;
  property.get_Value(&v);
  return v;
}

Snippet *read_conf(IHttpServer &server, PCWSTR cfg_path) {
  auto admin_manager = server.GetAdminManager();
  IAppHostElement *cfg_root_elem;
  auto res = admin_manager->GetAdminSection(
      (BSTR)L"system.webServer/datadogRum", const_cast<BSTR>(cfg_path),
      &cfg_root_elem);
  if (res != S_OK) {
    return nullptr;
  }

  const auto defer_root = defer([&] { cfg_root_elem->Release(); });

  IAppHostProperty *enabled_prop = get_property(cfg_root_elem, L"enabled");
  if (enabled_prop == nullptr) {
    // missing enabled attribute -> invalid format -> disabled
    return nullptr;
  }

  const auto defer_enabled_prop = defer([&] { enabled_prop->Release(); });

  const auto is_enabled = get_property_value(*enabled_prop).boolVal;
  if (!is_enabled) {
    return nullptr;
  }

  int cfg_version = 5;
  auto version_prop = get_property(cfg_root_elem, L"version");
  if (version_prop != nullptr) {
    cfg_version = get_property_value(*version_prop).iVal;
    version_prop->Release();
  }

  // Iterate on the SDK configuration
  std::unordered_map<std::string, std::string> rum_sdk_opts;

  IAppHostElementCollection *col = nullptr;
  if (cfg_root_elem->get_Collection(&col) == S_OK) {
    const auto defer_col = defer([&] { col->Release(); });

    DWORD n_elem = 0;
    col->get_Count(&n_elem);

    VARIANT item_idx;
    item_idx.vt = VT_I2;

    for (DWORD i = 0; i < n_elem; ++i) {
      item_idx.iVal = static_cast<SHORT>(i);

      IAppHostElement *option_element = nullptr;
      col->get_Item(item_idx, &option_element);
      if (option_element == nullptr) {
        continue;
      }

      const auto option_element_guard =
          defer([&] { option_element->Release(); });

      auto property = get_property(option_element, L"name");
      if (property == nullptr) {
        // TODO: log property `name` do not exist
        continue;
      }
      std::string opt_name =
          wstring_to_utf8(std::wstring(get_property_value(*property).bstrVal));

      property = get_property(option_element, L"value");
      if (property == nullptr) {
        continue;
      }

      std::string opt_value =
          wstring_to_utf8(std::wstring(get_property_value(*property).bstrVal));

      rum_sdk_opts.emplace(std::move(opt_name), std::move(opt_value));
    }
  }

  // Build the JSON representation of the configuration
  const std::string json_cfg = make_json_cfg(cfg_version, rum_sdk_opts);
  return snippet_create_from_json(json_cfg.c_str());
}
} // namespace

GlobalModule::GlobalModule(IHttpServer *server, DWORD server_version,
                           std::shared_ptr<Logger> logger)
    : server_(server), logger_(std::move(logger)) {
  assert(server_ != nullptr);

  // TODO(@dmehala): Add `rum` product in `app-started` event.
  datadog::telemetry::Configuration cfg;
  cfg.enabled = true;
  cfg.integration_name = "iis";
  cfg.integration_version = std::to_string(server_version);

  auto maybe_telemetry_cfg = datadog::telemetry::finalize_config(cfg);
  if (auto error = maybe_telemetry_cfg.if_error()) {
    logger_->error(std::format("Failed to configure the telemetry module: {}",
                               error->message));
  } else {
    std::vector<std::shared_ptr<datadog::telemetry::Metric>> rum_metrics{
        telemetry::injection_skip::already_injected,
        telemetry::injection_skip::invalid_content_type,
        telemetry::injection_skip::no_content,
        telemetry::injection_skip::compressed_html,
        telemetry::injection_succeed,
        telemetry::injection_failed,
        telemetry::configuration_succeed,
        telemetry::configuration_failed_invalid_json,
    };

    telemetry_ = std::make_unique<datadog::telemetry::Telemetry>(
        *maybe_telemetry_cfg, logger_, rum_metrics);
  }
}

void GlobalModule::Terminate() { delete this; }

GLOBAL_NOTIFICATION_STATUS
GlobalModule::OnGlobalApplicationStart(
    IHttpApplicationStartProvider *provider) {
  assert(server_ != nullptr);

  auto ctx = new ModuleContext;
  ctx->logger = logger_.get();

  auto app = provider->GetApplication();
  assert(app);

  auto config_path = app->GetAppConfigPath();
  logger_->info(std::format("Parsing configuration \"{}\" for app (id: {})",
                            wstring_to_utf8(config_path),
                            wstring_to_utf8(app->GetApplicationId())));
  Snippet *snippet = read_conf(*server_, config_path);
  if (snippet == nullptr) {
    logger_->error("Failed to load RUM configuration");
    ctx->js_snippet = nullptr;
  } else if (snippet->error_code != 0) {
    logger_->error(std::format("Failed to load RUM configuration: {}",
                               snippet->error_message));
    ctx->js_snippet = nullptr;
  } else {
    ctx->js_snippet = snippet;
    logger_->info("Configuration validated");
  }

  app->GetModuleContextContainer()->SetModuleContext(ctx, g_module_id);

  // NOTE(@dmehala): Keep a reference on the module context for
  // `OnGlobalConfigurationChange` event.
  configurations_.emplace(config_path, ctx);

  return GL_NOTIFICATION_CONTINUE;
}

GLOBAL_NOTIFICATION_STATUS
GlobalModule::OnGlobalApplicationStop(IHttpApplicationStopProvider *provider) {
  auto cfg_path = provider->GetApplication()->GetApplicationId();
  if (cfg_path == nullptr) {
    return GL_NOTIFICATION_CONTINUE;
  }

  configurations_.erase(cfg_path);
  return GL_NOTIFICATION_CONTINUE;
}

GLOBAL_NOTIFICATION_STATUS GlobalModule::OnGlobalConfigurationChange(
    IGlobalConfigurationChangeProvider *provider) {
  assert(provider != nullptr);
  assert(server_ != nullptr);

  auto cfg_path = provider->GetChangePath();
  if (cfg_path == nullptr) {
    assert(true);
    return GL_NOTIFICATION_CONTINUE;
  }

  logger_->info(std::format("Dispatching configuration update (\"{}\")",
                            wstring_to_utf8(cfg_path)));

  // NOTE(@dmehala): There's a mismatch between the config path given in
  // `OnGlobalApplicationStart` and the one here. This path is the common parent
  // config, we need to ensure the update is sent to all applications that
  // inherit from it.
  for (const auto &[path, ctx] : configurations_) {
    if (ctx == nullptr) {
      // NOTE(@dmehala): should not happend because configuration entries are
      // removed when an application is stopped. This should be considered as a
      // bug.
      // TODO(@dmehala): Report a telemetry log.
      assert(true);
      continue;
    }

    // Question(@dmehala): should the default behaviour to keep the old setting
    // if there's an issue with the configuration?
    if (std::wstring_view(path).starts_with(cfg_path)) {
      Snippet *snippet = read_conf(*server_, provider->GetChangePath());
      if (snippet->error_code != 0) {
        logger_->error(std::format(
            "Failed to load new RUM configuration: {}. Keep using the old one",
            snippet->error_message));
      } else {
        ctx->js_snippet = snippet;
      }
    }
  }

  return GL_NOTIFICATION_CONTINUE;
}

} // namespace datadog::rum
