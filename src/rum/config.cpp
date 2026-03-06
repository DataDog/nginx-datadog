#include "config.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <optional>
#include <string_view>

using namespace std::string_view_literals;

#include "config_internal.h"
#include "string_util.h"

namespace datadog::nginx::rum::internal {

std::optional<bool> parse_bool(std::string_view value) {
  std::string lower(value);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
    return true;
  }
  if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
    return false;
  }
  return std::nullopt;
}

rum_config_map
get_rum_config_from_env() {
  rum_config_map config;
  for (const auto& [env_name, config_key] : rum_env_mappings) {
    const char* value = std::getenv(env_name.data());
    if (value != nullptr && value[0] != '\0') {
      config[std::string(config_key)] = {std::string(value)};
    }
  }
  return config;
}

std::optional<bool> get_rum_enabled_from_env() {
  const char* value = std::getenv("DD_RUM_ENABLED");
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return parse_bool(value);
}

std::string make_rum_json_config(
    int config_version,
    const rum_config_map& config) {
  rapidjson::Document doc;
  doc.SetObject();

  rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
  doc.AddMember("majorVersion", config_version, allocator);

  rapidjson::Value rum(rapidjson::kObjectType);
  for (const auto& [key, values] : config) {
    if (values.empty()) continue;
    if (key == "sessionSampleRate" || key == "sessionReplaySampleRate") {
      char* endp;
      double val = std::strtod(values.front().c_str(), &endp);
      if (endp == values.front().c_str()) {
        // Not a valid number — pass as string and let the RUM SDK validate.
        rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                      rapidjson::Value(values.front().c_str(), allocator).Move(),
                      allocator);
      } else {
        rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                      rapidjson::Value(val).Move(), allocator);
      }
    } else if (key == "trackResources" || key == "trackLongTasks" ||
               key == "trackUserInteractions") {
      auto parsed = parse_bool(values.front());
      if (parsed.has_value()) {
        rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                      rapidjson::Value(*parsed).Move(), allocator);
      } else {
        // Not a recognized boolean — pass as string and let the RUM SDK
        // validate.
        rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                      rapidjson::Value(values.front().c_str(), allocator).Move(),
                      allocator);
      }
    } else {
      if (values.size() == 1) {
        rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                      rapidjson::Value(values.front().c_str(), allocator).Move(),
                      allocator);
      } else {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& entry : values) {
          array.PushBack(rapidjson::Value(entry.c_str(), allocator).Move(),
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

constexpr std::size_t err_buf_size = 256;

template <typename... Args>
char* conf_err(ngx_conf_t* cf, const char* fmt, Args... args) {
  auto* buf = static_cast<char*>(ngx_pcalloc(cf->pool, err_buf_size));
  if (buf == nullptr) {
    return const_cast<char*>("nginx-datadog: memory allocation failed");
  }
  ngx_snprintf(reinterpret_cast<u_char*>(buf), err_buf_size, fmt, args...);
  return buf;
}

using namespace datadog::nginx::rum::internal;

void apply_rum_config_tags(datadog::nginx::datadog_loc_conf_t* loc_conf,
                           const rum_config_map& config) {
  loc_conf->rum_remote_config_tag = "remote_config_used:false";
  if (auto it = config.find("applicationId");
      it != config.end() && !it->second.empty()) {
    loc_conf->rum_application_id_tag = "application_id:" + it->second[0];
  }
  if (auto it = config.find("remoteConfigurationId");
      it != config.end() && !it->second.empty()) {
    loc_conf->rum_remote_config_tag = "remote_config_used:true";
  }
}

char* set_config(ngx_conf_t* cf, ngx_command_t* command, void* conf) {
  auto& rum_config =
      *static_cast<rum_config_map*>(
          conf);

  if (cf->args->nelts < 2) {
    return conf_err(
        cf, "invalid number of arguments. Expected at least two arguments.");
  }

  ngx_str_t* arg_values = static_cast<ngx_str_t*>(cf->args->elts);

  std::string_view key = datadog::nginx::to_string_view(arg_values[0]);
  if (key.empty()) {
    return conf_err(cf, "empty key");
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
    std::string arg1_str(arg1);
    return conf_err(
        cf,
        "invalid version argument provided. Expected version in format "
        "'vN' (e.g. 'v5') but encountered '%s'.",
        arg1_str.c_str());
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
    return conf_err(
        cf, "failed to generate the RUM SDK script: missing version field");
  }

  auto snippet = std::unique_ptr<Snippet, decltype(&snippet_cleanup)>(
      snippet_create_from_json(json.c_str()), snippet_cleanup);

  if (snippet == nullptr || snippet->error_code) {
    return conf_err(cf, "failed to generate the RUM SDK script: %s",
                    snippet ? snippet->error_message : "null snippet");
  }

  loc_conf->rum_snippet = snippet.release();
  apply_rum_config_tags(loc_conf, rum_config);

  return NGX_CONF_OK;
}

void try_build_snippet_from_env(ngx_conf_t* cf,
                                datadog::nginx::datadog_loc_conf_t* loc_conf) {
  try {
    auto env_config = get_rum_config_from_env();
    if (env_config.empty()) return;

    auto json = make_rum_json_config(default_rum_config_version, env_config);
    if (json.empty()) {
      ngx_log_error(
          NGX_LOG_WARN, cf->log, 0,
          "nginx-datadog: DD_RUM_* environment variables were set but "
          "JSON config generation produced an empty result");
      return;
    }

    auto snippet = std::unique_ptr<Snippet, decltype(&snippet_cleanup)>(
        snippet_create_from_json(json.c_str()), snippet_cleanup);
    if (snippet == nullptr || snippet->error_code) {
      ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                    "nginx-datadog: failed to create RUM snippet from "
                    "environment variables: %s",
                    snippet ? snippet->error_message : "null snippet");
      return;
    }

    loc_conf->rum_snippet = snippet.release();
    apply_rum_config_tags(loc_conf, env_config);
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception& exception) {
    ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                  "nginx-datadog: failed to build RUM snippet from environment "
                  "variables: %s",
                  exception.what());
  }
}

void resolve_rum_enable_from_env(ngx_conf_t* cf,
                                 datadog::nginx::datadog_loc_conf_t* loc_conf) {
  const char* raw = std::getenv("DD_RUM_ENABLED");
  if (raw == nullptr || raw[0] == '\0') {
    // Not set — auto-enable if a valid snippet exists (from env vars or
    // inherited). This avoids requiring users to set DD_RUM_ENABLED explicitly
    // when DD_RUM_* config vars are already provided.
    if (loc_conf->rum_snippet != nullptr) {
      loc_conf->rum_enable = 1;
    }
    return;
  }

  auto parsed = parse_bool(raw);
  if (!parsed.has_value()) {
    ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                  "nginx-datadog: unrecognized DD_RUM_ENABLED value '%s'; "
                  "expected true/false/1/0/yes/no/on/off",
                  raw);
    return;
  }

  if (*parsed && loc_conf->rum_snippet == nullptr) {
    ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                  "nginx-datadog: DD_RUM_ENABLED is true but no valid RUM "
                  "snippet is available; RUM injection will be disabled");
    loc_conf->rum_enable = 0;
  } else {
    loc_conf->rum_enable = static_cast<ngx_flag_t>(*parsed);
  }
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
    try_build_snippet_from_env(cf, child);
  }

  // Determine rum_enable when neither child nor parent set it explicitly.
  if (!child_explicit && !parent_explicit) {
    resolve_rum_enable_from_env(cf, child);
  }

  return NGX_CONF_OK;
}

std::vector<std::string_view> get_environment_variable_names() {
  std::vector<std::string_view> names;
  names.reserve(rum_env_mappings.size() + 1);
  names.push_back("DD_RUM_ENABLED"sv);
  for (const auto& [env_name, config_key] : rum_env_mappings) {
    names.push_back(env_name);
  }
  return names;
}

}  // namespace datadog::nginx::rum
