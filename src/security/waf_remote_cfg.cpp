#include "waf_remote_cfg.h"

#include <datadog/remote_config/capability.h>
#include <datadog/remote_config/product.h>
#include <datadog/string_view.h>
#include <ddwaf.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/error/error.h>
#include <rapidjson/pointer.h>
#include <rapidjson/rapidjson.h>

#include <charconv>
#include <initializer_list>
#include <ostream>
#include <regex>
#include <stdexcept>

#include "ddwaf_obj.h"
#include "library.h"
#include "ngx_logger.h"

namespace rc = datadog::remote_config;
namespace dnsec = datadog::nginx::security;
namespace dn = datadog::nginx;
using namespace std::literals;
using dd::StringView;
using Product = rc::product::Flag;
using Capability = rc::capability::Flag;

namespace {

// A configuration key has the form:
// (datadog/<org_id> | employee)/<PRODUCT>/<config_id>/<name>"
class ParsedConfigKey {
 public:
  explicit ParsedConfigKey(std::string key) : key_{std::move(key)} {
    parse_config_key();
  }
  ParsedConfigKey(const ParsedConfigKey &oth) : ParsedConfigKey(oth.key_) {
    parse_config_key();
  }
  ParsedConfigKey &operator=(const ParsedConfigKey &oth) {
    if (&oth != this) {
      key_ = oth.key_;
      parse_config_key();
    }
    return *this;
  }
  ParsedConfigKey(ParsedConfigKey &&oth) noexcept
      : key_{std::move(oth.key_)},
        source_{oth.source()},
        org_id_{oth.org_id_},
        product_{oth.product_},
        config_id_{oth.config_id_},
        name_{oth.name_} {
    oth.source_ = {};
    oth.org_id_ = 0;
    oth.product_ = Product::UNKNOWN;
    oth.config_id_ = {};
    oth.name_ = {};
  }
  ParsedConfigKey &operator=(ParsedConfigKey &&oth) noexcept {
    if (&oth != this) {
      key_ = std::move(oth.key_);
      source_ = oth.source_;
      org_id_ = oth.org_id_;
      product_ = oth.product_;
      config_id_ = oth.config_id_;
      name_ = oth.name_;
      oth.source_ = {};
      oth.org_id_ = 0;
      oth.product_ = Product::UNKNOWN;
      oth.config_id_ = {};
      oth.name_ = {};
    }
    return *this;
  }
  ~ParsedConfigKey() = default;

  bool operator==(const ParsedConfigKey &other) const {
    return key_ == other.key_;
  }

  struct Hash {
    std::size_t operator()(const ParsedConfigKey &k) const {
      return std::hash<std::string>()(k.key_);
    }
  };

  // lifetime of return values is that of the data pointer in key_
  StringView full_key() const { return {key_}; }
  StringView source() const { return source_; }
  std::uint64_t org_id() const { return org_id_; }
  Product product() const { return product_; }
  StringView config_id() const { return config_id_; }
  StringView name() const { return name_; }

 private:
  void parse_config_key();

  std::string key_;
  StringView source_;
  std::uint64_t org_id_{};
  Product product_{Product::UNKNOWN};
  StringView config_id_;
  StringView name_;
};

class CurrentAppSecConfig {
  bool dirty_{};
  bool failed_{};

 public:
  void set_dd_config(const dnsec::ddwaf_map_obj &config) {
    static const ParsedConfigKey key_bundled_rule_data{
        std::string{datadog::nginx::security::Library::kBundledRuleset}};

    set_config(key_bundled_rule_data, config);
  }

  void set_config(const ParsedConfigKey &key,
                  const dnsec::ddwaf_map_obj &new_config) {
    dnsec::Library::Diagnostics diag{{}};
    bool res =
        dnsec::Library::update_waf_config(key.full_key(), new_config, diag);

    if (!res) {
      failed_ = true;
      throw std::runtime_error{
          std::string{"Library::update_waf_config() failed for "} +
          std::string{key.full_key()}};
    }
    dirty_ = true;
  }

  void remove_config(const ParsedConfigKey &key) {
    bool res = dnsec::Library::remove_waf_config(key.full_key());
    if (!res) {
      failed_ = true;
      throw std::runtime_error{
          std::string{"Library::remove_waf_config() failed for "} +
          std::string{key.full_key()}};
    }

    dirty_ = true;
  }

  struct Status {
    bool dirty;
    bool failed;
  };

  Status consume_status() {
    Status s{dirty_, failed_};
    dirty_ = failed_ = false;
    return s;
  }
};

class JsonParsedConfig {
 public:
  JsonParsedConfig(const std::string &content) {
    json_.Parse(content.c_str());
    if (json_.HasParseError()) {
      throw std::runtime_error{
          "JSON parse error: " +
          std::string{rapidjson::GetParseError_En(json_.GetParseError())} +
          " at offset " + std::to_string(json_.GetErrorOffset())};
    }
  }

 protected:
  rapidjson::Document json_;
};

template <typename Self>
class ProductListener : public rc::Listener {
 protected:
  ProductListener(dn::NgxLogger &logger) : logger_{logger} {}

  rc::Products get_products() /* const */ override final {
    return Self::kProduct;
  }

  rc::Capabilities get_capabilities() /* const */ override final {
    rc::Capabilities caps{};
    for (auto c : Self::kCapabilities) {
      caps |= c;
    }
    return caps;
  }

  dd::Optional<std::string> on_update(
      const Configuration &config) override final {
    try {
      static_cast<Self *>(this)->on_update_impl(ParsedConfigKey{config.path},
                                                config.content);
      logger_.log_debug([&key = config.path](std::ostream &oss) {
        oss << "successfully applied config: " << key;
      });
      return {};
    } catch (const std::exception &e) {
      logger_.log_error([&key = config.path, &e](std::ostream &oss) {
        oss << "failed to update config: " << key << ": " << e.what();
      });
      return e.what();
    }
  };

  void on_revert(const Configuration &config) override final {
    try {
      static_cast<Self *>(this)->on_revert_impl(ParsedConfigKey{config.path});
      logger_.log_debug([&key = config.path](std::ostream &oss) {
        oss << "successfully reverted config: " << key;
      });
    } catch (const std::exception &e) {
      logger_.log_error([&key = config.path, &e](std::ostream &oss) {
        oss << "failed to revert config: " << key << ": " << e.what();
      });
    }
  }

  void on_post_process() override {}

 protected:
  dn::NgxLogger &logger_;
};

class AsmFeaturesListener : public ProductListener<AsmFeaturesListener> {
  class AppSecFeatures : public JsonParsedConfig {
   public:
    using JsonParsedConfig::JsonParsedConfig;

    bool asm_enabled() {
      const rapidjson::Pointer json_pointer("/asm/enabled");
      assert(json_pointer.IsValid());
      if (auto val = json_pointer.Get(json_)) {
        return val->GetBool();
      } else {
        return false;  // not present -> disabled (default state)
      }
    }
  };

 public:
  static constexpr inline auto kProduct = Product::ASM_FEATURES;
  static constexpr inline auto kCapabilities = {Capability::ASM_ACTIVATION};

  AsmFeaturesListener(dn::NgxLogger &logger) : ProductListener{logger} {}

  void on_update_impl(const ParsedConfigKey &key, const std::string &content) {
    if (key.config_id() != "asm_features_activation"sv) {
      return;
    }

    AppSecFeatures features{content};
    bool new_state = features.asm_enabled();
    bool old_state = dnsec::Library::active();
    if (new_state == old_state) {
      return;
    }

    dnsec::Library::set_active(new_state);
  };

  void on_revert_impl(const ParsedConfigKey &key) {
    return on_update_impl(key, std::string{"{}"});
  };
};

class AsmDDListener : public ProductListener<AsmDDListener> {
 public:
  static constexpr inline auto kProduct = Product::ASM_DD;
  static constexpr inline auto kCapabilities = {
      Capability::ASM_DD_RULES,
      Capability::ASM_IP_BLOCKING,
      Capability::ASM_REQUEST_BLOCKING,
  };

  AsmDDListener(CurrentAppSecConfig &cur_appsec_cfg,
                std::shared_ptr<dnsec::ddwaf_owned_map> default_config,
                dn::NgxLogger &logger)
      : ProductListener{logger},
        cur_appsec_cfg_{cur_appsec_cfg},
        default_config_{default_config} {}

  void on_update_impl(const ParsedConfigKey &key, const std::string &content) {
    // convert content to rapidjson::Document:
    rapidjson::Document doc;
    rapidjson::ParseResult result = doc.Parse(content.c_str(), content.size());
    if (!result) {
      throw std::invalid_argument(
          "failed to parse remote config for asm_dd: " +
          std::string{rapidjson::GetParseError_En(result.Code())});
    }

    auto new_config = dnsec::json_to_object(doc, dnsec::kConfigMaxDepth);
    if (!new_config.get().is_map()) {
      throw std::invalid_argument("config for asm_dd is not a map");
    }

    try {
      cur_appsec_cfg_.set_dd_config(dnsec::ddwaf_map_obj{new_config.get()});
    } catch (const std::exception &e) {
      cur_appsec_cfg_.set_dd_config(default_config_.get()->get());
      throw e;
    }
  }

  void on_revert_impl(const ParsedConfigKey &key) {
    cur_appsec_cfg_.set_dd_config(default_config_.get()->get());
  }

 private:
  CurrentAppSecConfig &cur_appsec_cfg_;
  std::shared_ptr<dnsec::ddwaf_owned_map> default_config_;
};

class AsmDataListener : public ProductListener<AsmDataListener> {
 public:
  static constexpr inline auto kProduct = Product::ASM_DATA;
  static constexpr inline std::initializer_list<Capability> kCapabilities = {};

  AsmDataListener(CurrentAppSecConfig &cur_appsec_cfg, dn::NgxLogger &logger)
      : ProductListener{logger}, cur_appsec_cfg_{cur_appsec_cfg} {}

  void on_update_impl(const ParsedConfigKey &key, const std::string &content) {
    rapidjson::Document doc;
    rapidjson::ParseResult result = doc.Parse(content.c_str(), content.size());
    if (!result) {
      throw std::invalid_argument(
          "failed to parse remote config for asm_data: " +
          std::string{rapidjson::GetParseError_En(result.Code())});
    }

    if (!doc.IsObject()) {
      throw std::invalid_argument("asm_data remote config not an object");
    }

    dnsec::ddwaf_owned_map new_data{
        dnsec::json_to_object(doc, dnsec::kConfigMaxDepth)};
    cur_appsec_cfg_.set_config(key, new_data.get());
  }

  void on_revert_impl(const ParsedConfigKey &key) {
    cur_appsec_cfg_.remove_config(key);
  }

 private:
  CurrentAppSecConfig &cur_appsec_cfg_;
};

class AsmUserConfigListener : public ProductListener<AsmUserConfigListener> {
 public:
  static constexpr inline auto kProduct = Product::ASM;
  static constexpr inline auto kCapabilities = {Capability::ASM_CUSTOM_RULES};

  AsmUserConfigListener(CurrentAppSecConfig &cur_appsec_cfg,
                        dn::NgxLogger &logger)
      : ProductListener{logger}, cur_appsec_cfg_{cur_appsec_cfg} {}

  void on_update_impl(const ParsedConfigKey &key, const std::string &content) {
    rapidjson::Document doc;
    rapidjson::ParseResult result = doc.Parse(content.c_str(), content.size());
    if (!result) {
      throw std::invalid_argument(
          "failed to parse remote config for ASM product (user config): " +
          std::string{rapidjson::GetParseError_En(result.Code())});
    }

    dnsec::ddwaf_owned_map new_data{
        dnsec::json_to_object(doc, dnsec::kConfigMaxDepth)};
    cur_appsec_cfg_.set_config(key, new_data.get());
  }

  void on_revert_impl(const ParsedConfigKey &key) {
    cur_appsec_cfg_.remove_config(key);
  }

 private:
  CurrentAppSecConfig &cur_appsec_cfg_;
};

template <Product... Ps>
class ConfigurationEndListener : public rc::Listener {
 public:
  ConfigurationEndListener(std::function<void()> func)
      : func_{std::move(func)} {}

  rc::Products get_products() override { return (Ps | ... | 0); }

  rc::Capabilities get_capabilities() override { return {}; }

  dd::Optional<std::string> on_update(const Configuration &) override {
    return {};
  };

  void on_revert(const Configuration &config) override {}

  void on_post_process() override { func_(); }

 private:
  std::function<void()> func_;
};

class AppSecConfigService {
  std::shared_ptr<dnsec::ddwaf_owned_map> default_config_;
  CurrentAppSecConfig current_config_;
  std::shared_ptr<dn::NgxLogger> logger_;

  static inline std::unique_ptr<AppSecConfigService> instance_;  // NOLINT

  AppSecConfigService(dnsec::ddwaf_owned_map default_config,
                      std::shared_ptr<datadog::nginx::NgxLogger> logger)
      : default_config_{std::make_shared<dnsec::ddwaf_owned_map>(
            std::move(default_config))},
        logger_{std::move(logger)} {}

 public:
  AppSecConfigService(const AppSecConfigService &) = delete;
  AppSecConfigService &operator=(const AppSecConfigService &) = delete;
  AppSecConfigService(AppSecConfigService &&) = delete;
  AppSecConfigService &operator=(AppSecConfigService &&) = delete;
  ~AppSecConfigService() = default;

  static void initialize(dnsec::ddwaf_owned_map default_config,
                         std::shared_ptr<datadog::nginx::NgxLogger> logger) {
    if (instance_) {
      throw std::logic_error{"AppSecConfigService already initialized"};
    }
    instance_ = std::unique_ptr<AppSecConfigService>{
        new AppSecConfigService{std::move(default_config), std::move(logger)}};
  }

  static bool has_instance() { return static_cast<bool>(instance_); }

  static AppSecConfigService &instance() {
    if (!instance_) {
      throw std::logic_error{"AppSecConfigService not initialized"};
    }
    return *instance_;
  }

  void subscribe_to_remote_config(datadog::tracing::DatadogAgentConfig &ddac,
                                  bool accept_cfg_update,
                                  bool is_subscribe_activation) {
    if (is_subscribe_activation) {
      subscribe_activation(ddac);
    }

    if (accept_cfg_update) {
      subscribe_rules_and_data(ddac);

      ddac.remote_configuration_listeners.emplace_back(
          new ConfigurationEndListener<Product::ASM, Product::ASM_DATA,
                                       Product::ASM_DD>([this] {
            auto status = current_config_.consume_status();

            if (status.failed) {
              if (status.dirty) {
                ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                              "Recreating WAF instance despite some updates "
                              "having failed");
              } else {
                ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                              "WAF instance would be recreated, but all "
                              "the updates errored");
              }
            }

            if (status.dirty) {
              bool res = dnsec::Library::regenerate_handle();
              if (!res) {
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                              "Failed to regenerate WAF instance");
              }
            }
          }));
    }
  }

 private:
  void subscribe_activation(datadog::tracing::DatadogAgentConfig &ddac) {
    // ASM_FEATURES
    ddac.remote_configuration_listeners.emplace_back(
        new AsmFeaturesListener(*logger_));
  }

  void subscribe_rules_and_data(datadog::tracing::DatadogAgentConfig &ddac) {
    // ASM_DD
    ddac.remote_configuration_listeners.emplace_back(
        new AsmDDListener(current_config_, default_config_, *logger_));

    // ASM_DATA
    ddac.remote_configuration_listeners.emplace_back(
        new AsmDataListener(current_config_, *logger_));

    // ASM
    ddac.remote_configuration_listeners.emplace_back(
        new AsmUserConfigListener(current_config_, *logger_));
  }
};

template <typename SubMatch>
StringView submatch_to_sv(const SubMatch &sub_match) {
  return StringView{&*sub_match.first,
                    static_cast<std::size_t>(sub_match.length())};
}

void ParsedConfigKey::parse_config_key() {
  static std::regex const rgx{
      "(?:datadog/(\\d+)|employee)/([^/]+)/([^/]+)/([^/]+)"};
  std::smatch smatch;
  if (!std::regex_match(key_, smatch, rgx)) {
    throw std::invalid_argument("Invalid config key: " + key_);
  }

  if (key_[0] == 'd') {
    source_ = "datadog";
    auto [ptr, ec] =
        std::from_chars(&*smatch[1].first, &*smatch[1].second, org_id_);
    if (ec != std::errc{} || ptr != &*smatch[1].second) {
      throw std::invalid_argument("Invalid org_id in config key " + key_ +
                                  ": " +
                                  std::string{submatch_to_sv(smatch[1])});
    }
  } else {
    source_ = "employee";
    org_id_ = 0;
  }

  StringView const product_sv{submatch_to_sv(smatch[2])};
  product_ = rc::parse_product(product_sv);

  config_id_ = submatch_to_sv(smatch[3]);
  name_ = submatch_to_sv(smatch[4]);
}

}  // namespace

namespace datadog::nginx::security {

void register_default_config(
    ddwaf_owned_map default_config,
    std::shared_ptr<datadog::nginx::NgxLogger> logger) {
  AppSecConfigService::initialize(std::move(default_config), std::move(logger));
}

void register_with_remote_cfg(datadog::tracing::DatadogAgentConfig &ddac,
                              bool accept_cfg_update,
                              bool subscribe_activation) {
  if (!AppSecConfigService::has_instance()) {
    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                  "No subscription to remote config for the WAF: no previous "
                  "succesful initialization of the WAF");
    return;
  }
  AppSecConfigService::instance().subscribe_to_remote_config(
      ddac, accept_cfg_update, subscribe_activation);
}
}  // namespace datadog::nginx::security
