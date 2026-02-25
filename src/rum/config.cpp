#include "config.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <charconv>
#include <cstdlib>
#include <optional>
#include <string_view>

#include "config_internal.h"
#include "string_util.h"

using namespace std::string_view_literals;

namespace datadog::nginx::rum::internal {

const env_mapping rum_env_mappings[] = {
    {"DD_RUM_APPLICATION_ID"sv, "applicationId"sv},
    {"DD_RUM_CLIENT_TOKEN"sv, "clientToken"sv},
    {"DD_RUM_SITE"sv, "site"sv},
    {"DD_RUM_SERVICE"sv, "service"sv},
    {"DD_RUM_ENV"sv, "env"sv},
    {"DD_RUM_VERSION"sv, "version"sv},
    {"DD_RUM_SESSION_SAMPLE_RATE"sv, "sessionSampleRate"sv},
    {"DD_RUM_SESSION_REPLAY_SAMPLE_RATE"sv, "sessionReplaySampleRate"sv},
    {"DD_RUM_TRACK_RESOURCES"sv, "trackResources"sv},
    {"DD_RUM_TRACK_LONG_TASKS"sv, "trackLongTasks"sv},
    {"DD_RUM_TRACK_USER_INTERACTIONS"sv, "trackUserInteractions"sv},
    {"DD_RUM_REMOTE_CONFIGURATION_ID"sv, "remoteConfigurationId"sv},
};

const std::size_t rum_env_mappings_size =
    sizeof(rum_env_mappings) / sizeof(rum_env_mappings[0]);

std::unordered_map<std::string, std::vector<std::string>>
get_rum_config_from_env() {
  std::unordered_map<std::string, std::vector<std::string>> config;
  for (std::size_t i = 0; i < rum_env_mappings_size; ++i) {
    const auto& mapping = rum_env_mappings[i];
    const char* value = std::getenv(std::string(mapping.env_name).c_str());
    if (value != nullptr && value[0] != '\0') {
      config[std::string(mapping.config_key)] = {std::string(value)};
    }
  }
  return config;
}

std::optional<bool> get_rum_enabled_from_env() {
  const char* value = std::getenv("DD_RUM_ENABLED");
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  std::string_view sv(value);
  if (sv == "true" || sv == "1" || sv == "yes" || sv == "on") {
    return true;
  }
  if (sv == "false" || sv == "0" || sv == "no" || sv == "off") {
    return false;
  }
  return std::nullopt;
}

std::string make_rum_json_config(
    int config_version,
    const std::unordered_map<std::string, std::vector<std::string>>& config) {
  rapidjson::Document doc;
  doc.SetObject();

  rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
  doc.AddMember("majorVersion", config_version, allocator);

  rapidjson::Value rum(rapidjson::kObjectType);
  for (const auto& [key, values] : config) {
    if (key == "sessionSampleRate" || key == "sessionReplaySampleRate") {
      char* endp;
      double val = std::strtod(values[0].c_str(), &endp);
      if (endp == values[0].c_str()) {
        // Not a valid number — pass as string and let the Rust SDK validate.
        rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                      rapidjson::Value(values[0].c_str(), allocator).Move(),
                      allocator);
      } else {
        rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                      rapidjson::Value(val).Move(), allocator);
      }
    } else if (key == "trackResources" || key == "trackLongTasks" ||
               key == "trackUserInteractions") {
      auto b = (values[0] == "true" ? true : false);
      rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                    rapidjson::Value(b).Move(), allocator);
    } else {
      if (values.size() == 1) {
        rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                      rapidjson::Value(values[0].c_str(), allocator).Move(),
                      allocator);
      } else {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& e : values) {
          array.PushBack(rapidjson::Value(e.c_str(), allocator).Move(),
                         allocator);
        }

        rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                      array.Move(), allocator);
      }
    }
  }

  doc.AddMember("rum", rum, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  return buffer.GetString();
}

std::optional<int> parse_rum_version(std::string_view config_version) {
  if (config_version.size() < 2 || !config_version.starts_with("v")) {
    return std::nullopt;
  }

  int version_number;
  auto [ptr, ec] = std::from_chars(
      config_version.data() + 1, config_version.data() + config_version.size(),
      version_number);
  if (ptr == config_version.data() || ec == std::errc::invalid_argument ||
      ec == std::errc::result_out_of_range) {
    return std::nullopt;
  }

  return version_number;
}

}  // namespace datadog::nginx::rum::internal

namespace {

using namespace datadog::nginx::rum::internal;

char* set_config(ngx_conf_t* cf, ngx_command_t* command, void* conf) {
  auto& rum_config =
      *static_cast<std::unordered_map<std::string, std::vector<std::string>>*>(
          conf);

  if (cf->args->nelts < 2) {
    char* err_msg =
        static_cast<char*>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf(
        (u_char*)err_msg, 256,
        "invalid number of arguments. Expected at least two arguments.");
    return err_msg;
  }

  ngx_str_t* arg_values = (ngx_str_t*)(cf->args->elts);

  std::string_view key = datadog::nginx::to_string_view(arg_values[0]);
  if (key.empty()) {
    char* err_msg =
        static_cast<char*>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char*)err_msg, 256, "empty key");
    return err_msg;
  }

  std::vector<std::string> values;
  for (size_t i = 1; i < cf->args->nelts; ++i) {
    values.emplace_back(datadog::nginx::to_string(arg_values[i]));
  }

  rum_config[std::string(key)] = std::move(values);
  return NGX_CONF_OK;
}

}  // namespace

namespace datadog::nginx::rum {

using namespace internal;

char* on_datadog_rum_config(ngx_conf_t* cf, ngx_command_t* command,
                            void* conf) {
  auto* loc_conf = static_cast<datadog::nginx::datadog_loc_conf_t*>(conf);

  auto* values = static_cast<ngx_str_t*>(cf->args->elts);

  auto arg1 = datadog::nginx::to_string_view(values[1]);
  auto config_version = parse_rum_version(arg1);
  if (!config_version) {
    auto* err_msg =
        static_cast<char*>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char*)err_msg, 256,
                 "invalid version argument provided. Expected version 'v5' but "
                 "encountered '%s'. Please ensure you are using the correct "
                 "version format 'v5'",
                 arg1);
    return err_msg;
  }

  auto rum_config = get_rum_config_from_env();

  ngx_conf_t save = *cf;
  cf->handler = set_config;
  cf->handler_conf = &rum_config;
  char* status = ngx_conf_parse(cf, NULL);
  *cf = save;

  if (status != NGX_CONF_OK) {
    return status;
  }

  auto json = make_rum_json_config(*config_version, rum_config);
  if (json.empty()) {
    auto* err_msg =
        static_cast<char*>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf(
        (u_char*)err_msg, 256,
        "failed to generate the RUM SDK script: missing version field");
    return err_msg;
  }

  Snippet* snippet = snippet_create_from_json(json.c_str());

  if (snippet->error_code) {
    auto* err_msg =
        static_cast<char*>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char*)err_msg, 256,
                 "failed to generate the RUM SDK script: %s",
                 snippet->error_message);
    snippet_cleanup(snippet);
    return err_msg;
  }

  loc_conf->rum_snippet = snippet;

  loc_conf->rum_remote_config_tag = "remote_config_used:false";

  if (auto it = rum_config.find("applicationId");
      it != rum_config.end() && !it->second.empty()) {
    loc_conf->rum_application_id_tag = "application_id:" + it->second[0];
  }

  if (auto it = rum_config.find("remoteConfigurationId");
      it != rum_config.end() && !it->second.empty()) {
    loc_conf->rum_remote_config_tag = "remote_config_used:true";
  }

  return NGX_CONF_OK;
}

char* datadog_rum_merge_loc_config(ngx_conf_t* cf,
                                   datadog::nginx::datadog_loc_conf_t* parent,
                                   datadog::nginx::datadog_loc_conf_t* child) {
  bool child_explicit = (child->rum_enable != NGX_CONF_UNSET);
  bool parent_explicit = (parent->rum_enable != NGX_CONF_UNSET);

  ngx_conf_merge_value(child->rum_enable, parent->rum_enable, 0);

  if (child->rum_snippet == nullptr) {
    child->rum_snippet = parent->rum_snippet;
  }

  if (child->rum_application_id_tag.empty()) {
    child->rum_application_id_tag = parent->rum_application_id_tag;
  }

  if (child->rum_remote_config_tag.empty()) {
    child->rum_remote_config_tag = parent->rum_remote_config_tag;
  }

  // If no snippet was inherited from the directive, try building one from env
  // vars.
  if (child->rum_snippet == nullptr) {
    try {
      auto env_config = get_rum_config_from_env();
      if (!env_config.empty()) {
        auto json = make_rum_json_config(5, env_config);
        if (!json.empty()) {
          Snippet* snippet = snippet_create_from_json(json.c_str());
          if (snippet != nullptr && !snippet->error_code) {
            child->rum_snippet = snippet;
            child->rum_remote_config_tag = "remote_config_used:false";
            if (auto it = env_config.find("applicationId");
                it != env_config.end() && !it->second.empty()) {
              child->rum_application_id_tag = "application_id:" + it->second[0];
            }
            if (auto it = env_config.find("remoteConfigurationId");
                it != env_config.end() && !it->second.empty()) {
              child->rum_remote_config_tag = "remote_config_used:true";
            }
          } else {
            ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                          "nginx-datadog: failed to create RUM snippet from "
                          "environment variables: %s",
                          snippet ? snippet->error_message : "null snippet");
            if (snippet != nullptr) {
              snippet_cleanup(snippet);
            }
          }
        }
      }
    } catch (const std::exception& e) {
      ngx_log_error(
          NGX_LOG_WARN, cf->log, 0,
          "nginx-datadog: invalid DD_RUM_* environment variable value: %s",
          e.what());
    }
  }

  // Determine rum_enable when neither child nor parent set it explicitly.
  if (!child_explicit && !parent_explicit) {
    auto env_enabled = get_rum_enabled_from_env();
    if (env_enabled.has_value()) {
      child->rum_enable = *env_enabled ? 1 : 0;
    } else if (child->rum_snippet != nullptr) {
      // Auto-enable if a valid snippet exists (from env vars or inherited).
      child->rum_enable = 1;
    }
  }

  return NGX_OK;
}

std::vector<std::string_view> environment_variable_names() {
  std::vector<std::string_view> names;
  names.reserve(rum_env_mappings_size + 1);
  names.push_back("DD_RUM_ENABLED"sv);
  for (std::size_t i = 0; i < rum_env_mappings_size; ++i) {
    names.push_back(rum_env_mappings[i].env_name);
  }
  return names;
}

}  // namespace datadog::nginx::rum
