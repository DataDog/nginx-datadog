#include "waf_remote_cfg.h"

#include <datadog/remote_config.h>
#include <ngx_log.h>

#include "datadog/json_fwd.hpp"
#include "library.h"

namespace rc = datadog::tracing::remote_config;
namespace dns = datadog::nginx::security;

namespace {
class JsonParsedConfig {
  nlohmann::json json;
  public:
   JsonParsedConfig(const std::string &content)
       : json(nlohmann::json::parse(content)) {}
};
class AsmDDListener : public rc::ProductListener {
  class AppSecFeatures {
    nlohmann::json &json;
    public:
    AppSecFeatures(nlohmann::json &json) : json(json) {}

    bool asm_enabled() {
      try {
      return json.at("/asm/enabled"_json_pointer).get<bool>();
      } catch (nlohmann::json::exception &) {
        return false;
      }
    }
  };
public:
  void on_config_update(const rc::ParsedConfigKey &key,
                                const std::string &content) override {

                                }
  void on_config_remove(const rc::ParsedConfigKey &key) override {

  };

  rc::CapabilitiesSet capabilities() const override {
    return {};
  };

 private:
  static std::shared_ptr<dns::waf_handle> orig_handle_;  // NOLINT
};
}  // namespace

namespace datadog::nginx::security {

void register_with_remote_cfg() {

}
} // namespace datadog::nginx::security
