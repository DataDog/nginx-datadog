#include "library.h"

#include "string_util.h"

extern "C" {
#include <ngx_core.h>
#include <ngx_cycle.h>
#include <ngx_log.h>
#include <ngx_string.h>
}

#include <ddwaf.h>
#include <rapidjson/schema.h>

#include <fstream>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "../datadog_conf.h"
#include "blocking.h"
#include "context.h"
#include "ddwaf_obj.h"
#include "util.h"

extern "C" {
#define INCBIN_SILENCE_BITCODE_WARNING
#include "incbin.h"
}

extern "C" {
INCBIN(char, RecommendedJson, "recommended.json");
}

using namespace std::literals;
namespace dns = datadog::nginx::security;

namespace {

static constexpr ddwaf_config waf_config{
    .limits =
        {
            .max_container_size = 150,
            .max_container_depth = 10,
            .max_string_length = 4096,
        },
    .free_fn = nullptr,
};

auto parse_rule_json(std::string_view json) -> dns::ddwaf_owned_map {
  rapidjson::Document document;
  rapidjson::ParseResult const result =
      document.Parse(json.data(), json.size());
  if (!result) {
    std::string_view error_msg{rapidjson::GetParseError_En(result.Code())};
    throw std::invalid_argument{"malformed json: " + std::string{error_msg}};
  }
  if (!document.IsObject()) {
    throw std::invalid_argument("invalid json rule (not a json object)");
  }

  return dns::ddwaf_owned_map{dns::json_to_object(document)};
}

auto read_rule_file(std::string_view filename) -> dns::ddwaf_owned_map {
  std::ifstream rule_file(filename.data(), std::ios::in);
  if (!rule_file) {
    throw std::system_error(errno, std::generic_category());
  }

  // Create a buffer equal to the file size
  rule_file.seekg(0, std::ios::end);
  std::string buffer(rule_file.tellg(), '\0');
  buffer.resize(rule_file.tellg());
  rule_file.seekg(0, std::ios::beg);

  auto buffer_size = buffer.size();
  if (buffer_size > static_cast<typeof(buffer_size)>(
                        std::numeric_limits<std::streamsize>::max())) {
    throw std::runtime_error{"rule file is too large"};
  }

  rule_file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  buffer.resize(rule_file.gcount());
  rule_file.close();

  return parse_rule_json(buffer);
}

int ddwaf_log_level_to_nginx(DDWAF_LOG_LEVEL level) noexcept {
  switch (level) {
    case DDWAF_LOG_TRACE:
    case DDWAF_LOG_DEBUG:
      return NGX_LOG_DEBUG;
    case DDWAF_LOG_INFO:
      return NGX_LOG_INFO;
    case DDWAF_LOG_WARN:
      return NGX_LOG_WARN;
    case DDWAF_LOG_ERROR:
      return NGX_LOG_ERR;
    default:
      return NGX_LOG_NOTICE;
  }
}

DDWAF_LOG_LEVEL ngx_log_level_to_ddwaf(int level) noexcept {
  switch (level) {
    case NGX_LOG_DEBUG:
      return DDWAF_LOG_DEBUG;
    case NGX_LOG_INFO:
    case NGX_LOG_NOTICE:
      return DDWAF_LOG_INFO;
    case NGX_LOG_WARN:
    case NGX_LOG_STDERR:
      return DDWAF_LOG_WARN;
    case NGX_LOG_ERR:
    case NGX_LOG_CRIT:
    case NGX_LOG_ALERT:
    case NGX_LOG_EMERG:
      return DDWAF_LOG_ERROR;
    default:
      return DDWAF_LOG_ERROR;
  }
}

void ddwaf_log(DDWAF_LOG_LEVEL level, const char *function, const char *file,
               unsigned line, const char *message,
               uint64_t message_len) noexcept {
  int const log_level = ddwaf_log_level_to_nginx(level);
  ngx_str_t const message_ngxs = dns::ngx_stringv(
      std::string_view{message, static_cast<std::size_t>(message_len)});
  ngx_log_error(log_level, ngx_cycle->log, 0, "ddwaf: %V at %s on %s:%d",
                &message_ngxs, function, file, line);
}

std::string ddwaf_subdiagnostics_to_str(const dns::ddwaf_map_obj &, std::string_view);
std::string ddwaf_diagnostics_to_str(const dns::ddwaf_map_obj &top) {
  std::string ret;
  ret += ddwaf_subdiagnostics_to_str(top, "rules"sv);
  ret += "; ";
  ret += ddwaf_subdiagnostics_to_str(top, "processors"sv);
  ret += "; ";
  ret += ddwaf_subdiagnostics_to_str(top, "exclusions"sv);
  ret += "; ";
  ret += ddwaf_subdiagnostics_to_str(top, "rules_data"sv);

  return ret;
}

std::string ddwaf_subdiagnostics_to_str(const dns::ddwaf_map_obj &top, std::string_view key) {
  auto maybe_rm = top.get_opt<dns::ddwaf_map_obj>(key);
  if (!maybe_rm) {
    return "no diagnostics for " + std::string{key};
  }

  dns::ddwaf_map_obj &m = *maybe_rm;
  std::size_t loaded_count = m.get_opt<dns::ddwaf_arr_obj>("loaded"sv)
      .value_or(dns::ddwaf_arr_obj{})
      .size();
  std::size_t failed_count = m.get_opt<dns::ddwaf_arr_obj>("failed"sv)
      .value_or(dns::ddwaf_arr_obj{})
      .size();

  std::string ret{key};
  ret += "{loaded(";
  ret += std::to_string(loaded_count);
  ret += ") failed(";
  ret += std::to_string(failed_count);
  ret += ") errors(";

  auto errors = m.get_opt<dns::ddwaf_map_obj>("errors"sv);
  if (errors) {
    bool first = true;
    for (auto &&err_kp : *errors) {
      if (!first) {
        ret += ", ";
      } else {
        first = false;
      }

      ret += err_kp.key(); // error message
      ret += " => [";
      bool first = true;
      for (auto&&v: dns::ddwaf_arr_obj{err_kp}) {
        if (!first) {
          ret += ", ";
        } else {
          first = false;
        }
        ret += dns::ddwaf_str_obj{v}.value();
      }
      ret += "]";
    }
  }
  ret += ")}";
  return ret;
}
}  // namespace

namespace datadog::nginx::security {

waf_handle::waf_handle(ddwaf_handle h, const ddwaf_map_obj &merged_actions) {
  assert(h != nullptr);
  handle_ = h;
  action_info_map_ = extract_actions(merged_actions);
}

/*
 * {
 *   ...,
 *   actions: [
 *     { id: "...", type: "...", parameters: { ...} }, ...
 *   ]
 * }
 */
waf_handle::action_info_map_t waf_handle::extract_actions(
    const ddwaf_object &ruleset) {
  ddwaf_map_obj const root{ruleset};
  std::optional<ddwaf_arr_obj> actions = root.get_opt<ddwaf_arr_obj>("actions");
  if (!actions) {
    return default_actions();
  }

  action_info_map_t action_info_map{default_actions()};
  for (auto &&v : *actions) {
    ddwaf_map_obj const action_spec{v};

    std::string_view id =
        ddwaf_str_obj{action_spec.get<ddwaf_str_obj>("id")}.value();
    std::string_view const type =
        ddwaf_str_obj{action_spec.get<ddwaf_str_obj>("type")}.value();
    std::optional<ddwaf_map_obj> parameters =
        action_spec.get_opt<ddwaf_map_obj>("parameters");

    std::map<std::string, action_info::str_or_int, std::less<>> parameters_map;
    if (parameters) {
      for (auto &&pvalue : *parameters) {
        auto pkey = pvalue.key();
        if (pvalue.type == DDWAF_OBJ_STRING) {
          action_info::str_or_int pvalue_variant{
              std::in_place_type<std::string>, pvalue.string_val_unchecked()};
          parameters_map.emplace(pkey, std::move(pvalue_variant));
        } else if (pvalue.is_numeric()) {
          action_info::str_or_int pvalue_variant{
              std::in_place_type<int>, pvalue.numeric_val_unchecked<int>()};
          parameters_map.emplace(pkey, std::move(pvalue_variant));
        }
      }
    }
    action_info_map.emplace(
        id, action_info{std::string{type}, std::move(parameters_map)});
  }

  return action_info_map;
}

std::optional<library::string_and_hash> library::custom_ip_header_;
std::shared_ptr<waf_handle> library::handle_{nullptr};
std::atomic<bool> library::active_{true};

std::optional<ddwaf_owned_map> library::initialize_security_library(
    const datadog_main_conf_t &conf) {
  if (!conf.appsec_enabled) {
    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                  "datadog security library is explicitly disabled");
    return std::nullopt;
  }

  std::string_view ruleset_file{to_string_view(conf.appsec_ruleset_file)};
  ddwaf_owned_map ruleset;
  if (!ruleset_file.empty()) {
    try {
      ruleset = read_rule_file(ruleset_file);
    } catch (const std::exception &e) {
      throw std::runtime_error(std::string{"failed to read ruleset "} + "at " +
                               std::string{ruleset_file} + ": " + e.what());
    }
  } else {
    try {
      ruleset = parse_rule_json(
          std::string_view{gRecommendedJsonData, gRecommendedJsonSize});
    } catch (const std::exception &e) {
      throw std::runtime_error{
          "failed to parse embedded recommended ruleset: " +
          std::string{e.what()}};
    }
  }
  ddwaf_set_log_cb(ddwaf_log,
                   ngx_log_level_to_ddwaf(ngx_cycle->log->log_level));
  libddwaf_ddwaf_owned_obj<ddwaf_map_obj> diag{{}};
  ddwaf_handle h = ddwaf_init(&ruleset.get(), &waf_config, &diag.get());
  if (!h) {
    throw std::runtime_error{"call to ddwaf_init failed:" +
                             ddwaf_diagnostics_to_str(diag.get())};
  }

  std::size_t num_loaded_rules = diag.get()
                                     .get_opt<dns::ddwaf_map_obj>("rules")
                                     .value_or(dns::ddwaf_map_obj{})
                                     .get_opt<dns::ddwaf_arr_obj>("loaded"sv)
                                     .value_or(dns::ddwaf_arr_obj{})
                                     .size();
  ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, "ddwaf_init loaded %uz rules",
                num_loaded_rules);

  auto actions = ruleset.get().get_opt<ddwaf_map_obj>("actions");
  library::handle_ =
      std::make_shared<waf_handle>(h, actions.value_or(ddwaf_map_obj{}));
  library::set_active(conf.appsec_enabled == 1);

  std::string_view template_html_path{
      to_string_view(conf.appsec_http_blocked_template_html)};
  std::string_view template_json_path{
      to_string_view(conf.appsec_http_blocked_template_json)};
  blocking_service::initialize(template_html_path, template_json_path);

  if (conf.custom_client_ip_header.len > 0) {
    std::string_view ccip_sv = to_string_view(conf.custom_client_ip_header);
    library::string_and_hash sah{
      std::string{ccip_sv},
      ngx_hash_ce(ccip_sv)
    };
    library::custom_ip_header_ = std::move(sah);
  }

  return ruleset;
}

void library::set_active(bool value) noexcept {
  active_.store(value, std::memory_order_relaxed);
  ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                "datadog security library made %s",
                value ? "active" : "inactive");
}
void library::set_handle(std::shared_ptr<waf_handle> handle) {
  std::atomic_store(&handle_, handle);
  ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                "WAF configuration updated");
}

bool library::update_ruleset(const ddwaf_map_obj &spec) {
  std::shared_ptr<waf_handle> cur_h{get_handle_uncond()};

  if (!cur_h) {
    throw std::runtime_error{"no handle to update"};
  }

  libddwaf_ddwaf_owned_obj<ddwaf_map_obj> diag{{}};
  ddwaf_handle new_h = ddwaf_update(cur_h->get(), &spec, &diag.get());
  if (!new_h) {
    throw std::runtime_error{"call to ddwaf_update failed:" +
                             ddwaf_diagnostics_to_str(diag.get())};
  }

  if (ngx_cycle->log->log_level & NGX_LOG_DEBUG_HTTP) {
    std::string diag_str = ddwaf_diagnostics_to_str(diag.get());
    ngx_str_t str = ngx_stringv(diag_str);
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0,
                  "ddwaf_update succeeded: %V", &str);
  }

  std::optional<ddwaf_map_obj> actions =
      spec.get_opt<ddwaf_map_obj>("actions"sv);

  auto handle =
      std::make_shared<waf_handle>(new_h, actions.value_or(ddwaf_map_obj{}));
  set_handle(std::move(handle));
  return true;
}

std::vector<std::string_view> library::environment_variable_names() {
  return {// These environment variable names are taken from
          // `tracer_options.cpp` and `tracer.cpp` in the `dd-opentracing-cpp`
          // repository. I did `git grep '"DD_\w\+"' -- src/` in the
          // `dd-opentracing-cpp` repository.
          "DD_APPSEC_ENABLED", "DD_APPSEC_RULES"};
}

}  // namespace datadog::nginx::security
