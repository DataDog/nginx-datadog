#include "config.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <charconv>

#include "string_util.h"

namespace {

char *set_config(ngx_conf_t *cf, ngx_command_t *command, void *conf) {
  auto *rum_config =
      static_cast<std::unordered_map<std::string, std::string> *>(conf);

  if (cf->args->nelts < 2) {
    char *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256,
                 "invalid number of arguments. Expected exactly two: a key and "
                 "a value.");
    return err_msg;
  }

  ngx_str_t *values = (ngx_str_t *)(cf->args->elts);

  std::string_view key = datadog::nginx::to_string_view(values[0]);
  if (key.empty()) {
    char *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256, "empty key");
    return err_msg;
  }

  std::string_view value = datadog::nginx::to_string_view(values[1]);
  if (value.empty()) {
    char *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256, "empty value");
    return err_msg;
  }

  (*rum_config)[std::string(key)] = std::string(value);
  return NGX_CONF_OK;
}

static std::string make_rum_json_config(
    int config_version,
    const std::unordered_map<std::string, std::string> &config) {
  rapidjson::Document doc;
  doc.SetObject();

  rapidjson::Document::AllocatorType &allocator = doc.GetAllocator();
  doc.AddMember("majorVersion", config_version, allocator);

  rapidjson::Value rum(rapidjson::kObjectType);
  for (const auto &[key, value] : config) {
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

}  // namespace

extern "C" {

char *on_datadog_rum_config(ngx_conf_t *cf, ngx_command_t *command,
                            void *conf) {
  auto *loc_conf = static_cast<datadog::nginx::datadog_loc_conf_t *>(conf);

  auto *values = static_cast<ngx_str_t *>(cf->args->elts);

  auto arg1 = datadog::nginx::to_string_view(values[1]);
  auto config_version = parse_rum_version(arg1);
  if (!config_version) {
    auto *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256,
                 "invalid version argument provided. Expected version 'v5' but "
                 "encountered '%s'. Please ensure you are using the correct "
                 "version format 'v5'",
                 arg1);
    return err_msg;
  }

  std::unordered_map<std::string, std::string> rum_config;

  ngx_conf_t save = *cf;
  cf->handler = set_config;
  cf->handler_conf = &rum_config;
  char *status = ngx_conf_parse(cf, NULL);
  *cf = save;

  if (status != NGX_CONF_OK) {
    return status;
  }

  auto json = make_rum_json_config(*config_version, rum_config);
  if (json.empty()) {
    auto *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf(
        (u_char *)err_msg, 256,
        "failed to generate the RUM SDK script: missing version field");
    return err_msg;
  }

  Snippet *snippet = snippet_create_from_json(json.c_str());

  if (snippet->error_code) {
    auto *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256,
                 "failed to generate the RUM SDK script: %s",
                 snippet->error_message);
    return err_msg;
  }

  loc_conf->rum_snippet = snippet;
  return NGX_CONF_OK;
}

char *datadog_rum_merge_loc_config(ngx_conf_t *cf,
                                   datadog::nginx::datadog_loc_conf_t *parent,
                                   datadog::nginx::datadog_loc_conf_t *child) {
  ngx_conf_merge_value(child->rum_enable, parent->rum_enable, 0);
  if (child->rum_snippet == nullptr) {
    child->rum_snippet = parent->rum_snippet;
  }

  return NGX_OK;
}
}
