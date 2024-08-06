#include "config.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "discovery.h"
#include "ngx_http_datadog_module.h"
#include "string_util.h"

namespace {

char *set_config(ngx_conf_t *cf, ngx_command_t *command, void *conf) {
  auto *rum_config =
      static_cast<std::unordered_map<std::string, std::string> *>(conf);

  if (cf->args->nelts < 2) {
    char *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256, "it's key-value bro. RTFM ffs");
    return err_msg;
  }

  ngx_str_t *values = (ngx_str_t *)(cf->args->elts);

  std::string_view key = datadog::nginx::to_string_view(values[0]);
  if (key.empty()) {
    char *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256, "key is empty dude. RTFM ffs");
    return err_msg;
  }

  std::string_view value = datadog::nginx::to_string_view(values[1]);
  if (value.empty()) {
    // TODO: warn the bro, he's prob doing something he did not intended
  }

  (*rum_config)[std::string(key)] = std::string(value);
  return NGX_CONF_OK;
}

static std::string make_rum_json_config(
    std::string_view config_version,
    const std::unordered_map<std::string, std::string> &config) {
  rapidjson::Document doc;
  doc.SetObject();

  rapidjson::Document::AllocatorType &allocator = doc.GetAllocator();
  doc.AddMember("majorVersion",
                rapidjson::Value(std::stoi(config_version.data() + 1)),
                allocator);

  rapidjson::Value rum(rapidjson::kObjectType);
  for (const auto &[key, value] : config) {
    if (key == "majorVersion") {
      continue;
    }
    if (key == "sessionSampleRate" || key == "sessionReplaySampleRate") {
      rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                    rapidjson::Value(std::stod(value.c_str())).Move(),
                    allocator);
    } else if (key == "trackResources" || key == "trackLongTasks" ||
               key == "trackUserInteractions") {
      rum.AddMember(rapidjson::Value(key.c_str(), allocator).Move(),
                    rapidjson::Value(value == "true").Move(), allocator);
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

}  // namespace

extern "C" {

char *on_datadog_rum_json_config(ngx_conf_t *cf, ngx_command_t *command,
                                 void *conf) {
  auto *loc_conf = static_cast<datadog::nginx::datadog_loc_conf_t *>(conf);

  ngx_str_t *values = (ngx_str_t *)(cf->args->elts);
  std::string_view file_location = datadog::nginx::to_string_view(values[1]);

  FILE *fp = fopen(file_location.data(), "r");
  if (fp == NULL) {
    auto *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256, "error opening file \"%s\"",
                 file_location);
    return err_msg;
  }

  fseek(fp, 0, SEEK_END);
  size_t file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  char *file_content = (char *)malloc(file_size * sizeof(char));
  if (file_content == NULL) {
    fclose(fp);

    auto *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256,
                 "failed to read configuration file \"%s\"",
                 file_location.data());
    return err_msg;
  }

  auto bytes_read = fread(file_content, sizeof(char), file_size, fp);
  (void)bytes_read;
  fclose(fp);

  Snippet *snippet = snippet_create_from_json(file_content);

  if (snippet->error_code) {
    auto *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256,
                 "failed to generate the RUM SDK script: %s",
                 snippet->error_message);
    return err_msg;
  }

  loc_conf->rum_snippet = snippet;
  loc_conf->rum_config_file =
      datadog::nginx::to_ngx_str(cf->pool, file_location);

  return NGX_CONF_OK;
}

char *on_datadog_directive(ngx_conf_t *cf, ngx_command_t *command, void *conf) {
  auto *loc_conf = static_cast<datadog::nginx::datadog_loc_conf_t *>(conf);

  ngx_str_t *values = (ngx_str_t *)(cf->args->elts);
  std::string_view v = datadog::nginx::to_string_view(values[1]);

  if (v == "off") {
    loc_conf->rum_enable = 0;
    return NGX_CONF_OK;
  }

  auto main_conf = static_cast<datadog::nginx::datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));

  auto default_app_id = datadog::rum::init(*main_conf);

  if (default_app_id.empty()) {
    return NGX_CONF_OK;
  }

  const std::unordered_map<std::string, std::string> rum_config{
      {"applicationId", default_app_id},
      {"clientToken", datadog::nginx::to_string(main_conf->client_token)},
      {"site", "datadoghq.eu"},
      {"service", "default_nginx"},
      {"env", "production"},
      {"version", "1.0.0"},
      {"sessionSampleRate", "100"},
      {"trackResources", "true"},
      {"trackLongTasks", "true"},
      {"trackUserInteractions", "true"},
  };

  auto json = make_rum_json_config("v5", rum_config);
  if (json.empty()) {
    auto *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf(
        (u_char *)err_msg, 256,
        "failed to generate the RUM SDK script: missing version field");
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

  loc_conf->rum_enable = 1;
  loc_conf->rum_snippet = snippet;

  return NGX_CONF_OK;
}

char *on_datadog_rum_config(ngx_conf_t *cf, ngx_command_t *command,
                            void *conf) {
  auto *loc_conf = static_cast<datadog::nginx::datadog_loc_conf_t *>(conf);

  auto *values = static_cast<ngx_str_t *>(cf->args->elts);

  std::string_view config_version = datadog::nginx::to_string_view(values[1]);
  if (!config_version.starts_with('v')) {
    auto *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf((u_char *)err_msg, 256, "not a valid version");
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

  auto json = make_rum_json_config(config_version, rum_config);
  if (json.empty()) {
    auto *err_msg =
        static_cast<char *>(ngx_palloc(cf->pool, sizeof(char) * 256));
    ngx_snprintf(
        (u_char *)err_msg, 256,
        "failed to generate the RUM SDK script: missing version field");
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
  ngx_conf_merge_str_value(child->rum_config_file, parent->rum_config_file, "");
  if (child->rum_snippet == nullptr) {
    child->rum_snippet = parent->rum_snippet;
  }

  return NGX_OK;
}
}
