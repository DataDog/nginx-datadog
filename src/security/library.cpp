#include "library.h"

#include <atomic>
#include <string>

extern "C" {
#include <ngx_core.h>
#include <ngx_cycle.h>
#include <ngx_log.h>
#include <ngx_string.h>
}

#include <ddwaf.h>
#include <rapidjson/schema.h>

#include <fstream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "blocking.h"
#include "ddwaf_obj.h"
#include "util.h"

extern "C" {
#define INCBIN_SILENCE_BITCODE_WARNING
#include "incbin.h"
}

extern "C" {
INCBIN(char, RecommendedJson, "security/recommended.json");
}

using namespace std::literals;
namespace dnsec = datadog::nginx::security;

namespace {

static constexpr ddwaf_config kBaseWafConfig{
    .limits =
        {
            .max_container_size = 256,
            .max_container_depth = 20,
            .max_string_length = 4096,
        },
    .free_fn = nullptr,
};

auto parse_rule_json(std::string_view json) -> dnsec::ddwaf_owned_map {
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

  return dnsec::ddwaf_owned_map{
      dnsec::json_to_object(document, dnsec::kConfigMaxDepth)};
}

auto read_rule_file(std::string_view filename) -> dnsec::ddwaf_owned_map {
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
  if (buffer_size > static_cast<decltype(buffer_size)>(
                        std::numeric_limits<std::streamsize>::max())) {
    throw std::runtime_error{"rule file is too large"};
  }

  rule_file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  buffer.resize(rule_file.gcount());
  rule_file.close();

  return parse_rule_json(buffer);
}

dnsec::ddwaf_owned_map read_ruleset(
    std::optional<std::string_view> ruleset_file) {
  dnsec::ddwaf_owned_map ruleset;
  if (ruleset_file) {
    try {
      ruleset = read_rule_file(*ruleset_file);
    } catch (const std::exception &e) {
      throw std::runtime_error(std::string{"failed to read ruleset "} + "at " +
                               std::string{*ruleset_file} + ": " + e.what());
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

  return ruleset;
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
      if (level >= NGX_LOG_DEBUG_FIRST) {
        return DDWAF_LOG_DEBUG;
      }
      return DDWAF_LOG_ERROR;
  }
}

void ddwaf_log(DDWAF_LOG_LEVEL level, const char *function, const char *file,
               unsigned line, const char *message,
               uint64_t message_len) noexcept {
  int const log_level = ddwaf_log_level_to_nginx(level);
  ngx_str_t const message_ngxs = dnsec::ngx_stringv(
      std::string_view{message, static_cast<std::size_t>(message_len)});
  ngx_log_error(log_level, ngx_cycle->log, 0, "ddwaf: %V at %s on %s:%d",
                &message_ngxs, function, file, line);
}

std::string ddwaf_subdiagnostics_to_str(const dnsec::ddwaf_map_obj &,
                                        std::string_view);
std::string ddwaf_diagnostics_to_str(const dnsec::ddwaf_map_obj &top) {
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

std::string ddwaf_subdiagnostics_to_str(const dnsec::ddwaf_map_obj &top,
                                        std::string_view key) {
  auto maybe_rm = top.get_opt<dnsec::ddwaf_map_obj>(key);
  if (!maybe_rm) {
    return "no diagnostics for " + std::string{key};
  }

  dnsec::ddwaf_map_obj &m = *maybe_rm;
  std::size_t loaded_count = m.get_opt<dnsec::ddwaf_arr_obj>("loaded"sv)
                                 .value_or(dnsec::ddwaf_arr_obj{})
                                 .size();
  std::size_t failed_count = m.get_opt<dnsec::ddwaf_arr_obj>("failed"sv)
                                 .value_or(dnsec::ddwaf_arr_obj{})
                                 .size();

  std::string ret{key};
  ret += "{loaded(";
  ret += std::to_string(loaded_count);
  ret += ") failed(";
  ret += std::to_string(failed_count);
  ret += ") errors(";

  auto errors = m.get_opt<dnsec::ddwaf_map_obj>("errors"sv);
  if (errors) {
    bool first = true;
    for (auto &&err_kp : *errors) {
      if (!first) {
        ret += ", ";
      } else {
        first = false;
      }

      ret += err_kp.key();  // error message
      ret += " => [";
      bool first = true;
      for (auto &&v : dnsec::ddwaf_arr_obj{err_kp}) {
        if (!first) {
          ret += ", ";
        } else {
          first = false;
        }
        ret += dnsec::ddwaf_str_obj{v}.value();
      }
      ret += "]";
    }
  }
  ret += ")}";
  return ret;
}

struct DdwafBuilderFreeFunctor {
  void operator()(ddwaf_builder b) {
    if (b != nullptr) {
      ddwaf_builder_destroy(b);
    }
  }
};
class OwnedDdwafBuilder
    : public dnsec::FreeableResource<ddwaf_builder, DdwafBuilderFreeFunctor> {
  using FreeableResource::FreeableResource;
};

class UpdateableWafInstance {
 public:
  using Diagnostics = dnsec::Library::Diagnostics;

  bool init(const dnsec::ddwaf_map_obj &ruleset, ddwaf_config &config,
            Diagnostics &diagnostics);

  std::shared_ptr<dnsec::OwnedDdwafHandle> cur_handle() {
    return std::atomic_load_explicit(&cur_handle_, std::memory_order_acquire);
  }

  [[nodiscard]] bool add_or_update_config(std::string_view path,
                                          const dnsec::ddwaf_map_obj &ruleset,
                                          Diagnostics &diagnostics);

  [[nodiscard]] bool remove_config(std::string_view path);

  [[nodiscard]] bool update();

  [[nodiscard]] bool live() { return builder_.get() != nullptr; }

 private:
  std::mutex builder_mut_;
  OwnedDdwafBuilder builder_{nullptr};

  std::shared_ptr<dnsec::OwnedDdwafHandle> cur_handle_;
};

[[nodiscard]] bool UpdateableWafInstance::init(
    const dnsec::ddwaf_map_obj &ruleset, ddwaf_config &config,
    Diagnostics &diagnostics) {
  assert(!live());
  OwnedDdwafBuilder builder{ddwaf_builder_init(&config)};
  if (builder.get() == nullptr) {
    return false;
  }

  bool res = ddwaf_builder_add_or_update_config(
      *builder, dnsec::Library::kBundledRuleset.data(),
      dnsec::Library::kBundledRuleset.length(),
      const_cast<dnsec::ddwaf_map_obj *>(&ruleset), &*diagnostics);
  if (!res) {
    return false;
  }

  builder_ = std::move(builder);

  return update();
}

[[nodiscard]] bool UpdateableWafInstance::add_or_update_config(
    std::string_view path, const dnsec::ddwaf_map_obj &ruleset,
    Diagnostics &diagnostics) {
  std::lock_guard guard{builder_mut_};
  return ddwaf_builder_add_or_update_config(
      builder_.get(), path.data(), path.length(),
      const_cast<dnsec::ddwaf_map_obj *>(&ruleset), &*diagnostics);
}

[[nodiscard]] bool UpdateableWafInstance::remove_config(std::string_view path) {
  std::lock_guard guard{builder_mut_};
  return ddwaf_builder_remove_config(builder_.get(), path.data(),
                                     path.length());
}

[[nodiscard]] bool UpdateableWafInstance::update() {
  std::lock_guard guard{builder_mut_};
  ddwaf_handle new_instance = ddwaf_builder_build_instance(builder_.get());
  if (!new_instance) {
    return false;
  }

  std::shared_ptr<dnsec::OwnedDdwafHandle> new_sp =
      std::make_shared<dnsec::OwnedDdwafHandle>(new_instance);
  std::atomic_store_explicit(&cur_handle_, new_sp, std::memory_order::release);

  return true;
}
}  // namespace

namespace datadog::nginx::security {

class FinalizedConfigSettings {
  static constexpr ngx_uint_t kDefaultWafTimeoutUsec = 1000000;  // 100 ms
  static constexpr std::string_view kDefaultObfuscationKeyRegex =
      "(?i)(?:p(?:ass)?w(?:or)?d|pass(?:_?phrase)?|secret|(?:api_?|private_?|"
      "public_?)key)|token|consumer_?(?:id|key|secret)|sign(?:ed|ature)|bearer|"
      "authorization"sv;
  static constexpr std::string_view kDefaultObfuscationValueRegex =
      R"((?i)(?:p(?:ass)?w(?:or)?d|pass(?:_?phrase)?|secret|(?:api_?|private_?)"
      R"(|public_?|access_?|secret_?)key(?:_?id)?|token|consumer_?(?:id|key|)"
      R"(secret)|sign(?:ed|ature)?|auth(?:entication|orization)?)(?:\s*=[^;]|")"
      R"(\s*:\s*"[^"]+")|bearer\s+[a-z0-9._-]+|token:[a-z0-9]{13}|gh[opsu]_)"
      R"([0-9a-zA-Z]{36}|ey[I-L][\w=-]+\.ey[I-L][\w=-]+(?:\.[\w.+/=-]+)?|)"
      R"([-]{5}BEGIN[a-z\s]+PRIVATE\sKEY[-]{5}[^-]+[-]{5}END[a-z\s]+PRIVATE\s)"
      R"(KEY|ssh-rsa\s*[a-z0-9/.+]{100,})";

 public:
  enum class enable_status : std::uint8_t {
    ENABLED,
    DISABLED,
    UNSPECIFIED,
  };

  FinalizedConfigSettings(const datadog_main_conf_t &ngx_conf);
  FinalizedConfigSettings(const FinalizedConfigSettings &) = delete;
  FinalizedConfigSettings &operator=(const FinalizedConfigSettings &) = delete;
  FinalizedConfigSettings(FinalizedConfigSettings &&) = delete;
  FinalizedConfigSettings &operator=(FinalizedConfigSettings &&) = delete;
  ~FinalizedConfigSettings() = default;

  enable_status enable_status() const noexcept { return enable_status_; }

  auto ruleset_file() const { return non_empty_or_nullopt(ruleset_file_); }

  std::optional<HashedStringView> custom_ip_header() const {
    if (custom_ip_header_.empty()) {
      return std::nullopt;
    }
    return {{custom_ip_header_, custom_ip_header_hash_}};
  };

  auto blocked_template_json() const {
    return non_empty_or_nullopt(blocked_template_json_);
  }

  auto blocked_template_html() const {
    return non_empty_or_nullopt(blocked_template_html_);
  };

  auto waf_timeout() const { return waf_timeout_usec_; }

  const std::string &obfuscation_key_regex() const {
    return obfuscation_key_regex_;
  };

  const std::string &appsec_obfuscation_value_regex() const {
    return obfuscation_value_regex_;
  };

  std::optional<std::size_t> get_max_saved_output_data() const {
    return appsec_max_saved_output_data_;
  }

 private:
  // NOLINTNEXTLINE(readability-identifier-naming)
  using ev_t = std::vector<environment_variable_t>;

  // clang-format off
  static std::optional<bool> get_env_bool(const ev_t &evs, std::string_view name);
  static std::optional<std::string> get_env_str(const ev_t &evs, std::string_view name);
  static std::optional<std::string> get_env_str_maybe_empty(const ev_t &evs, std::string_view name);
  static std::optional<ngx_uint_t> get_env_unsigned(const ev_t &evs, std::string_view name);
  static std::string normalize_configured_header(std::string_view value);
  // clang-format on

  static std::optional<std::string_view> non_empty_or_nullopt(
      std::string_view sv) {
    if (sv.empty()) {
      return std::nullopt;
    }
    return {sv};
  }

  static std::optional<std::string_view> get_env(const ev_t &evs,
                                                 std::string_view name) {
    for (const environment_variable_t &ev_pair : evs) {
      if (ev_pair.name == name) {
        return {ev_pair.value};
      }
    }
    return std::nullopt;
  }

  enum enable_status enable_status_;
  std::string ruleset_file_;
  std::string custom_ip_header_;
  ngx_uint_t custom_ip_header_hash_;
  std::string blocked_template_json_;
  std::string blocked_template_html_;
  ngx_uint_t waf_timeout_usec_;
  std::string obfuscation_key_regex_;
  std::string obfuscation_value_regex_;
  std::optional<std::size_t> appsec_max_saved_output_data_;
};

FinalizedConfigSettings::FinalizedConfigSettings(
    const datadog_main_conf_t &ngx_conf) {
  auto &&evs = ngx_conf.environment_variables;

  if (ngx_conf.appsec_enabled == NGX_CONF_UNSET) {
    auto maybe_enabled = get_env_bool(evs, "DD_APPSEC_ENABLED"sv);
    if (!maybe_enabled.has_value()) {
      enable_status_ = enable_status::UNSPECIFIED;
    } else {
      enable_status_ =
          *maybe_enabled ? enable_status::ENABLED : enable_status::DISABLED;
    }
  } else {
    enable_status_ = ngx_conf.appsec_enabled == 1 ? enable_status::ENABLED
                                                  : enable_status::DISABLED;
  }

  if (ngx_conf.appsec_ruleset_file.len > 0) {
    ruleset_file_ = to_string_view(ngx_conf.appsec_ruleset_file);
  } else {
    ruleset_file_ = get_env_str(evs, "DD_APPSEC_RULES"sv).value_or("");
  }

  if (ngx_conf.appsec_http_blocked_template_json.len > 0) {
    blocked_template_json_ =
        to_string_view(ngx_conf.appsec_http_blocked_template_json);
  } else {
    blocked_template_json_ =
        get_env_str(evs, "DD_APPSEC_HTTP_BLOCKED_TEMPLATE_JSON"sv).value_or("");
  }

  if (ngx_conf.appsec_http_blocked_template_html.len > 0) {
    blocked_template_html_ =
        to_string_view(ngx_conf.appsec_http_blocked_template_html);
  } else {
    blocked_template_html_ =
        get_env_str(evs, "DD_APPSEC_HTTP_BLOCKED_TEMPLATE_HTML"sv).value_or("");
  }

  if (ngx_conf.custom_client_ip_header.len > 0) {
    custom_ip_header_ = normalize_configured_header(
        to_string_view(ngx_conf.custom_client_ip_header));
  } else {
    custom_ip_header_ = normalize_configured_header(
        get_env_str(evs, "DD_TRACE_CLIENT_IP_HEADER"sv).value_or(""));
  }
  custom_ip_header_hash_ = ngx_hash_ce(custom_ip_header_);

  if (ngx_conf.appsec_waf_timeout_ms == 0 ||
      ngx_conf.appsec_waf_timeout_ms == NGX_CONF_UNSET_MSEC) {
    waf_timeout_usec_ = get_env_unsigned(evs, "DD_APPSEC_WAF_TIMEOUT"sv)
                            .value_or(kDefaultWafTimeoutUsec);
  } else {
    waf_timeout_usec_ = ngx_conf.appsec_waf_timeout_ms * 1000;
  }

  if (ngx_conf.appsec_obfuscation_key_regex.data != nullptr) {
    obfuscation_key_regex_ =
        to_string_view(ngx_conf.appsec_obfuscation_key_regex);
  } else {
    obfuscation_key_regex_ =
        get_env_str_maybe_empty(evs,
                                "DD_APPSEC_OBFUSCATION_PARAMETER_KEY_REGEXP"sv)
            .value_or(std::string{kDefaultObfuscationKeyRegex});
  }

  if (ngx_conf.appsec_obfuscation_value_regex.data != nullptr) {
    obfuscation_value_regex_ =
        to_string_view(ngx_conf.appsec_obfuscation_value_regex);
  } else {
    obfuscation_value_regex_ =
        get_env_str_maybe_empty(
            evs, "DD_APPSEC_OBFUSCATION_PARAMETER_VALUE_REGEXP"sv)
            .value_or(std::string{kDefaultObfuscationValueRegex});
  }

  if (ngx_conf.appsec_max_saved_output_data != NGX_CONF_UNSET_SIZE) {
    appsec_max_saved_output_data_.emplace(
        ngx_conf.appsec_max_saved_output_data);
  }
}

std::optional<bool> FinalizedConfigSettings::get_env_bool(
    const ev_t &evs, std::string_view name) {
  auto maybe_value = get_env(evs, name);
  if (!maybe_value || maybe_value->empty()) {
    return std::nullopt;
  }

  std::string_view value_sv{*maybe_value};

  return value_sv == "1" || value_sv == "true" || value_sv == "yes" ||
         value_sv == "on";
}

std::optional<std::string> FinalizedConfigSettings::get_env_str(
    const ev_t &evs, std::string_view name) {
  auto maybe_value = get_env(evs, name);
  if (!maybe_value || maybe_value->empty()) {
    return std::nullopt;
  }

  return std::string{*maybe_value};
};

std::optional<std::string> FinalizedConfigSettings::get_env_str_maybe_empty(
    const ev_t &evs, std::string_view name) {
  auto maybe_value = get_env(evs, name);
  if (!maybe_value) {
    return std::nullopt;
  }

  return std::string{*maybe_value};
};

std::optional<ngx_uint_t> FinalizedConfigSettings::get_env_unsigned(
    const ev_t &evs, std::string_view name) {
  auto maybe_value = get_env(evs, name);
  if (!maybe_value || maybe_value->empty()) {
    return std::nullopt;
  }

  char *end;
  unsigned long value_int = std::strtoul(maybe_value->data(), &end, 10);
  if (*end != '\0') {
    return std::nullopt;
  }

  return static_cast<ngx_uint_t>(value_int);
}

std::string FinalizedConfigSettings::normalize_configured_header(
    std::string_view value) {
  // lowercase all the characters and replace _ with -
  std::string result;
  result.reserve(value.size());
  for (char c : value) {
    if (c == '_') {
      result.push_back('-');
    } else if (c >= 'A' && c <= 'Z') {
      result.push_back(c - 'A' + 'a');
    } else {
      result.push_back(c);
    }
  }
  return result;
}

std::unique_ptr<UpdateableWafInstance> upd_waf_instance{
    new UpdateableWafInstance{}};
std::atomic<bool> Library::active_{true};
std::unique_ptr<FinalizedConfigSettings> Library::config_settings_;

std::optional<ddwaf_owned_map> Library::initialize_security_library(
    const datadog_main_conf_t &ngx_conf) {
  config_settings_ = std::make_unique<FinalizedConfigSettings>(ngx_conf);
  const FinalizedConfigSettings &conf = *config_settings_;  // just an alias;

  if (conf.enable_status() ==
      FinalizedConfigSettings::enable_status::DISABLED) {
    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                  "datadog security library is explicitly disabled");
    return std::nullopt;
  }

  ddwaf_set_log_cb(ddwaf_log,
                   ngx_log_level_to_ddwaf(ngx_cycle->log->log_level));

  ddwaf_config waf_config = kBaseWafConfig;
  waf_config.obfuscator.key_regex = conf.obfuscation_key_regex().c_str();

  waf_config.obfuscator.value_regex =
      conf.appsec_obfuscation_value_regex().c_str();

  ddwaf_owned_map ruleset = read_ruleset(conf.ruleset_file());

  Diagnostics diag{{}};
  bool res = upd_waf_instance->init(ruleset.get(), waf_config, diag);
  if (!res) {
    throw std::runtime_error{"creation of original WAF handle failed: " +
                             ddwaf_diagnostics_to_str(diag.get())};
  }

  if (ngx_cycle->log->log_level >= NGX_LOG_INFO) {
    std::size_t num_loaded_rules =
        diag.get()
            .get_opt<dnsec::ddwaf_map_obj>("rules")
            .value_or(dnsec::ddwaf_map_obj{})
            .get_opt<dnsec::ddwaf_arr_obj>("loaded"sv)
            .value_or(dnsec::ddwaf_arr_obj{})
            .size();
    ngx_str_t source;
    if (auto rsf = conf.ruleset_file()) {
      source = ngx_stringv(*rsf);
    } else {
      source = ngx_stringv("embedded ruleset"sv);
    }
    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                  "AppSec loaded %uz rules from file %V", num_loaded_rules,
                  &source);
  }

  BlockingService::initialize(conf.blocked_template_html(),
                              conf.blocked_template_json());

  Library::set_active(conf.enable_status() ==
                      FinalizedConfigSettings::enable_status::ENABLED);

  return ruleset;
}

void Library::set_active(bool value) noexcept {
  active_.store(value, std::memory_order_relaxed);
  ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                "datadog security library made %s",
                value ? "active" : "inactive");
}

bool Library::active() noexcept {
  return active_.load(std::memory_order_relaxed);
}

[[nodiscard]] bool Library::update_waf_config(std::string_view path,
                                              const ddwaf_map_obj &spec,
                                              Diagnostics &diagnostics) {
  if (!upd_waf_instance->live()) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "Attempt to update non-live WAF config");
    return false;
  }
  bool res = upd_waf_instance->add_or_update_config(path, spec, diagnostics);

  if (res && ngx_cycle->log->log_level & NGX_LOG_DEBUG_HTTP) {
    std::string diag_str = ddwaf_diagnostics_to_str(*diagnostics);
    ngx_str_t str = ngx_stringv(diag_str);
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0,
                  "ddwaf_update succeeded: %V", &str);
  } else if (!res) {
    std::string diag_str = ddwaf_diagnostics_to_str(*diagnostics);
    ngx_str_t str = ngx_stringv(diag_str);
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "ddwaf_update failed: %V",
                  &str);
  }

  return res;
}

[[nodiscard]] bool Library::remove_waf_config(std::string_view path) {
  if (!upd_waf_instance->live()) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "Attempt to update non-live WAF config");
    return false;
  }
  bool res = upd_waf_instance->remove_config(path);

  if (res) {
    auto npath{ngx_stringv(path)};
    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                  "WAF configuration removed for %V", &npath);
  } else {
    auto npath{ngx_stringv(path)};
    ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                  "WAF configuration removal failed for %V", &npath);
  }
  return res;
}

[[nodiscard]] bool Library::regenerate_handle() {
  if (!upd_waf_instance->live()) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "Attempt to regenerate handle with non-live WAF config");
    return false;
  }

  bool res = upd_waf_instance->update();
  if (res) {
    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, "WAF configuration updated");
  } else {
    ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                  "WAF configuration update failed");
  }
  return res;
}

std::shared_ptr<OwnedDdwafHandle> Library::get_handle() {
  if (active_.load(std::memory_order_relaxed)) {
    return upd_waf_instance->cur_handle();
  }
  return {};
}

std::optional<HashedStringView> Library::custom_ip_header() {
  return config_settings_->custom_ip_header();
}

std::uint64_t Library::waf_timeout() {
  return static_cast<std::uint64_t>(config_settings_->waf_timeout());
}

std::vector<std::string_view> Library::environment_variable_names() {
  return {"DD_APPSEC_ENABLED"sv,
          "DD_APPSEC_RULES"sv,
          "DD_APPSEC_HTTP_BLOCKED_TEMPLATE_JSON"sv,
          "DD_APPSEC_HTTP_BLOCKED_TEMPLATE_HTML"sv,
          "DD_TRACE_CLIENT_IP_HEADER"sv,
          "DD_APPSEC_WAF_TIMEOUT"sv,
          "DD_APPSEC_OBFUSCATION_PARAMETER_KEY_REGEXP"sv,
          "DD_APPSEC_OBFUSCATION_PARAMETER_VALUE_REGEXP"sv};
}

std::optional<std::size_t> Library::max_saved_output_data() {
  return config_settings_->get_max_saved_output_data();
};
}  // namespace datadog::nginx::security
