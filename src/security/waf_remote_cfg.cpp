#include "waf_remote_cfg.h"

#include <datadog/remote_config.h>
#include <ddwaf.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/error/error.h>
#include <rapidjson/rapidjson.h>

#include <algorithm>
#include <datadog/json_fwd.hpp>
#include <optional>
#include <stdexcept>
#include <type_traits>

#include "library.h"
#include "security/ddwaf_memres.h"
#include "security/ddwaf_obj.h"

namespace rc = datadog::tracing::remote_config;
namespace dns = datadog::nginx::security;
using namespace std::literals;

namespace {
using datadog::tracing::RemoteConfigurationManager;

struct DirtyStatus {
  bool rules;
  bool custom_rules;
  bool rules_override;
  bool actions;
  bool data;
  bool exclusions;

  DirtyStatus operator|(DirtyStatus oth) {
    return {rules || oth.rules,
            custom_rules || oth.custom_rules,
            rules_override || oth.rules_override,
            actions || oth.actions,
            data || oth.data,
            exclusions || oth.exclusions};
  }

  DirtyStatus &operator |=(DirtyStatus oth) {
    return (*this = *this | oth);
  }

  static DirtyStatus allDirty() { return {true, true, true, true, true, true}; }

  bool is_any_dirty() const {
    return rules || custom_rules || rules_override || actions || data ||
           exclusions;
  }

  bool is_diry_for_ddwaf_update() const {
    return rules || custom_rules || rules_override || data || exclusions;
  }

  bool is_dirty_for_actions() const { return actions; }
};

class AppSecUserConfig {
  rc::ParsedConfigKey key_;
  dns::ddwaf_owned_map root_;
  // these are all arrays of maps: [{id: "...", ...}, ...]
  dns::ddwaf_arr_obj rules_override_;
  dns::ddwaf_arr_obj actions_;
  dns::ddwaf_arr_obj exclusions_;
  dns::ddwaf_arr_obj custom_rules_;

 public:
  AppSecUserConfig(rc::ParsedConfigKey key, dns::ddwaf_owned_map root)
      : key_{key}, root_{std::move(root)} {
    if (auto rules_override = root_.get().get_opt("rules_override"sv)) {
      rules_override_.shallow_copy_val_from(rules_override.value());
    }
    if (auto actions = root_.get().get_opt("actions"sv)) {
      actions_.shallow_copy_val_from(actions.value());
    }
    if (auto exclusions = root_.get().get_opt("exclusions"sv)) {
      exclusions_.shallow_copy_val_from(exclusions.value());
    }
    if (auto custom_rules = root_.get().get_opt("custom_rules"sv)) {
      custom_rules_.shallow_copy_val_from(custom_rules.value());
    }
  }

  const rc::ParsedConfigKey &key() const { return key_; }
  const dns::ddwaf_arr_obj &rules_override() const { return rules_override_; }
  const dns::ddwaf_arr_obj &actions() const { return actions_; }
  const dns::ddwaf_arr_obj &exclusions() const { return exclusions_; }
  const dns::ddwaf_arr_obj &custom_rules() const { return custom_rules_; }

  DirtyStatus dirty_effect() {
    // data not included here
    return {false,
            !custom_rules_.empty(),
            !rules_override_.empty(),
            !actions_.empty(),
            false,
            !exclusions_.empty()};
  }

  static AppSecUserConfig from_json(rc::ParsedConfigKey key,
                                    rapidjson::Document &json) {
    if (!json.IsObject()) {
      throw std::invalid_argument("user config json not a map");
    }

    dns::ddwaf_owned_obj oo = dns::json_to_object(json);
    return AppSecUserConfig{key, dns::ddwaf_owned_map{std::move(oo)}};
  }
};

class CollectedUserConfigs {
  std::vector<AppSecUserConfig> user_configs_;

  auto find_by_key(const rc::ParsedConfigKey &key) {
    return std::find_if(
        user_configs_.begin(), user_configs_.end(),
        [&key](const AppSecUserConfig &cfg) { return cfg.key() == key; });
  }

 public:
  DirtyStatus add_config(AppSecUserConfig new_config) {
    DirtyStatus removed_dirty = remove_config(new_config.key());
    DirtyStatus new_dirty = new_config.dirty_effect();

    user_configs_.emplace_back(std::move(new_config));

    std::sort(user_configs_.begin(), user_configs_.end(),
              [](const AppSecUserConfig &lhs, const AppSecUserConfig &rhs) {
                return lhs.key().full_key() < rhs.key().full_key();
              });

    return removed_dirty | new_dirty;
  }

  DirtyStatus remove_config(const rc::ParsedConfigKey &key) {
    auto it = find_by_key(key);
    if (it == user_configs_.end()) {
      return DirtyStatus{};  // nothing dirty
    }

    DirtyStatus ret = it->dirty_effect();
    user_configs_.erase(it);
    return ret;
  }

  const AppSecUserConfig &at(std::size_t index) const {
    return user_configs_.at(index);
  }

  std::size_t size() const { return user_configs_.size(); }

  auto begin() const { return user_configs_.begin(); }
  auto end() const { return user_configs_.end(); }
};

class CollectedAsmData {
  std::unordered_map<rc::ParsedConfigKey, dns::ddwaf_owned_arr /*arr*/,
                     rc::ParsedConfigKey::Hash>
      data_;

 public:
  void add_config(const rc::ParsedConfigKey &key,
                  dns::ddwaf_owned_arr new_config) {
    data_.emplace(key, std::move(new_config));
  }

  void remove_config(const rc::ParsedConfigKey &key) { data_.erase(key); }

  // returns [{id: "...", type: "...", data: [{...}, ...]}]
  // by merging the data value from all entries with the same id
  dns::ddwaf_arr_obj merged_data(dns::ddwaf_memres &memres) const {
    // first we need to group all the data by id
    auto grouped_entries =
        std::unordered_map<std::string_view, std::vector<dns::ddwaf_map_obj>>{};

    for (auto &&[key, owned_arr] : data_) {
      for (dns::ddwaf_obj &&data_entry_obj :
           dns::ddwaf_arr_obj{owned_arr.get()}) {
        // data_entry is a map with id, type, and data
        dns::ddwaf_map_obj data_entry{data_entry_obj};
        std::string_view id =
            data_entry.get<dns::ddwaf_str_obj>("id"sv).value();
        grouped_entries[id].push_back(data_entry);
      }
    }

    // then we need to merge the data
    dns::ddwaf_arr_obj ret;  // mixed ownership (data_ and memres)
    std::size_t num_ids = grouped_entries.size();
    ret.make_array(num_ids, memres);

    std::size_t i = 0;
    for (auto &&[id, vec] : grouped_entries) {
      // out has a format like this:
      // - id: ip_data
      //   type: ip_with_expiration
      //   data:
      //     - value: 192.168.1.1
      //       expiration: 555
      //     - ... merged from all entries
      dns::ddwaf_obj &out = ret.at_unchecked(i);

      dns::ddwaf_map_obj &merged_map = out.make_map(3, memres);
      // id is a string_view backed by one of the data etnries
      merged_map.get_entry_unchecked(0).set_key("id"sv).make_string(id);

      // check if the type is always the same for this id
      std::string_view first_type =
          vec.at(0).get<dns::ddwaf_str_obj>("type"sv).value();
      for (std::size_t j = 1; j < vec.size(); ++j) {
        if (vec.at(j).get<dns::ddwaf_str_obj>("type"sv).value() != first_type) {
          throw std::invalid_argument(
              "type is not the same for all data entries with id=" +
              std::string{id});
        }
      }
      merged_map.get_entry_unchecked(1).set_key("type"sv).make_string(
          first_type);

      // finally, the merged "data" key
      std::size_t total_data_entries = 0;
      for (dns::ddwaf_map_obj &obj : vec) {
        total_data_entries += obj.get<dns::ddwaf_arr_obj>("data"sv).size();
      }
      dns::ddwaf_arr_obj &merged_data =
          merged_map.get_entry_unchecked(2).set_key("data"sv).make_array(
              total_data_entries, memres);

      std::size_t k = 0;
      for (dns::ddwaf_map_obj &obj : vec) {
        dns::ddwaf_arr_obj cur_data = obj.get<dns::ddwaf_arr_obj>("data"sv);
        std::memcpy(&merged_data.at_unchecked(k), cur_data.array,
                    cur_data.size() * sizeof(dns::ddwaf_obj));
        k += cur_data.size();
      }
      assert(total_data_entries == k);
    }

    return ret;
  }
};

class CurrentAppSecConfig {
  std::shared_ptr<dns::ddwaf_owned_map> dd_config_;
  CollectedUserConfigs user_configs_;
  CollectedAsmData asm_data_;
  DirtyStatus dirty_status_;

 public:
  void set_dd_config(std::shared_ptr<dns::ddwaf_owned_map> new_config) {
    static const rc::ParsedConfigKey KEY_BUNDLED_RULE_DATA{
        "no_org/NONE/none/bundled_rule_data"};

    assert(new_config);

    dd_config_ = std::move(new_config);

    asm_data_.remove_config(KEY_BUNDLED_RULE_DATA);

    std::optional<dns::ddwaf_arr_obj> rules_data =
        dd_config().get_opt<dns::ddwaf_arr_obj>("rules_data"sv);
    if (rules_data) {
      dns::ddwaf_owned_arr rules_data_copy = dns::ddwaf_obj_clone(*rules_data);
      asm_data_.add_config(KEY_BUNDLED_RULE_DATA, std::move(rules_data_copy));
    }

    dirty_status_.rules = true;
  }

  void asm_data_add_config(const rc::ParsedConfigKey &key,
                           dns::ddwaf_owned_arr new_config) {
    asm_data_.add_config(key, std::move(new_config));
    dirty_status_.data = true;
  }

  void asm_data_remove_config(const rc::ParsedConfigKey &key) {
    asm_data_.remove_config(key);
    dirty_status_.data = true;
  }

  void user_config_add_config(AppSecUserConfig new_config) {
    dirty_status_ |= user_configs_.add_config(std::move(new_config));
  }

  void user_config_remove_config(const rc::ParsedConfigKey &key) {
    dirty_status_ |= user_configs_.remove_config(key);
  }

  // Main method
  // returns a mixed ownership object, with some static data,
  // some data owned by this object (indirectly), and newly allocated
  // data owned by the caller
  std::optional<dns::ddwaf_owned_map> merged_update_config() {
    if (!dirty_status_.is_any_dirty()) {
      return std::nullopt;
    }

    dns::ddwaf_owned_map ret;
    dns::ddwaf_map_obj &mo =
        ret.get().make_map(10, ret.memres());  // up to 10 entries
    dns::ddwaf_obj::nb_entries_t i = 0;
    if (dirty_status_.rules) {
      mo.get_entry_unchecked(i++)
          .set_key("metadata"sv)
          .shallow_copy_val_from(dd_config()
                                     .get_opt<dns::ddwaf_map_obj>("metadata"sv)
                                     .value_or(dns::ddwaf_map_obj{}));
      mo.get_entry_unchecked(i++).set_key("rules"sv).shallow_copy_val_from(
          dd_config().get_opt<dns::ddwaf_arr_obj>("rules"sv).value_or(
              dns::ddwaf_arr_obj{}));
      mo.get_entry_unchecked(i++)
          .set_key("processors"sv)
          .shallow_copy_val_from(
              dd_config()
                  .get_opt<dns::ddwaf_arr_obj>("processors"sv)
                  .value_or(dns::ddwaf_arr_obj{}));
      mo.get_entry_unchecked(i++)
          .set_key("scannners"sv)
          .shallow_copy_val_from(dd_config()
                                     .get_opt<dns::ddwaf_arr_obj>("scanners"sv)
                                     .value_or(dns::ddwaf_arr_obj{}));
    }

    if (dirty_status_.custom_rules) {
      mo.get_entry_unchecked(i++)
          .set_key("custom_rules"sv)
          .shallow_copy_val_from(get_merged_custom_rules(ret.memres()));
    }

    if (dirty_status_.exclusions || dirty_status_.rules) {
      mo.get_entry_unchecked(i++)
          .set_key("exclusions"sv)
          .shallow_copy_val_from(get_merged_exclusions(ret.memres()));
    }

    if (dirty_status_.rules_override || dirty_status_.rules) {
      mo.get_entry_unchecked(i++)
          .set_key("rules_override"sv)
          .shallow_copy_val_from(get_merged_rule_overrides(ret.memres()));
    }

    if (dirty_status_.data || dirty_status_.rules) {
      mo.get_entry_unchecked(i++)
          .set_key("rules_data"sv)
          .shallow_copy_val_from(asm_data_.merged_data(ret.memres()));
    }

    if (dirty_status_.actions || dirty_status_.actions) {
      mo.get_entry_unchecked(i++)
          .set_key("actions"sv)
          .shallow_copy_val_from(get_merged_actions(ret.memres()));
    }

    mo.get_entry_unchecked(i++)
        .set_key("version"sv)
        .shallow_copy_val_from(
            dd_config()
                .get_opt<dns::ddwaf_str_obj>("version"sv)
                .value_or(dns::ddwaf_str_obj{}.make_string("2.1"sv)));

    mo.nbEntries = i;

    dirty_status_ = DirtyStatus{};  // cleanse

    return std::move(ret);
  }

 private:
  const dns::ddwaf_map_obj &dd_config() { return dd_config_.get()->get(); }

  dns::ddwaf_arr_obj get_merged_custom_rules(dns::ddwaf_memres &memres) {
    std::vector<dns::ddwaf_arr_obj> arr_of_maps(user_configs_.size());
    for (auto &&uc : user_configs_) {
      arr_of_maps.push_back(uc.custom_rules());
    }

    return merge_maps_by_id_keep_latest(arr_of_maps, memres);
  }

  dns::ddwaf_arr_obj get_merged_exclusions(dns::ddwaf_memres &memres) {
    std::vector<dns::ddwaf_arr_obj> arr_of_maps(user_configs_.size() + 1);

    dns::ddwaf_arr_obj dd_rules_exclusions =
        dd_config()
            .get_opt<dns::ddwaf_arr_obj>("exclusions"sv)
            .value_or(dns::ddwaf_arr_obj{});

    arr_of_maps.push_back(dd_rules_exclusions);
    for (auto &&uc : user_configs_) {
      arr_of_maps.push_back(uc.exclusions());
    }

    return merge_maps_by_id_keep_latest(arr_of_maps, memres);
  }

  // does not include default actions
  dns::ddwaf_arr_obj get_merged_actions(dns::ddwaf_memres &memres) {
    std::vector<dns::ddwaf_arr_obj> arr_of_maps(user_configs_.size() + 1);

    dns::ddwaf_arr_obj dd_actions =
        dd_config()
            .get_opt<dns::ddwaf_arr_obj>("actions"sv)
            .value_or(dns::ddwaf_arr_obj{});

    arr_of_maps.push_back(dd_actions);
    for (auto &&uc : user_configs_) {
      arr_of_maps.push_back(uc.actions());
    }

    return merge_maps_by_id_keep_latest(arr_of_maps, memres);
  }

  dns::ddwaf_arr_obj get_merged_rule_overrides(dns::ddwaf_memres &memres) {
    std::vector<dns::ddwaf_arr_obj> arr_of_maps(user_configs_.size() + 1);

    dns::ddwaf_arr_obj dd_rules_override =
        dd_config()
            .get_opt<dns::ddwaf_arr_obj>("rules_override"sv)
            .value_or(dns::ddwaf_arr_obj{});

    arr_of_maps.push_back(dd_rules_override);
    for (auto &&uc : user_configs_) {
      arr_of_maps.push_back(uc.rules_override());
    }

    // plain merge; overrides have no id
    std::size_t total_count = 0;
    for (auto &&arr : arr_of_maps) {
      total_count += arr.size();
    }

    dns::ddwaf_arr_obj ret;
    ret.make_array(total_count, memres);
    std::size_t i = 0;
    for (auto &&arr : arr_of_maps) {
      for (auto &&obj : arr) {
        ret.at_unchecked(i++).shallow_copy_val_from(obj);
      }
    }
    assert(i == total_count);

    return ret;
  }

  dns::ddwaf_arr_obj merge_maps_by_id_keep_latest(
      const std::vector<dns::ddwaf_arr_obj> &arr_of_maps,
      dns::ddwaf_memres &memres) {
    std::unordered_map<std::string_view /*id*/, dns::ddwaf_map_obj> merged{};

    for (const dns::ddwaf_arr_obj &arr : arr_of_maps) {
      for (const dns::ddwaf_obj &obj : arr) {
        dns::ddwaf_map_obj map{obj};
        std::string_view id = map.get<dns::ddwaf_str_obj>("id"sv).value();
        merged[id] = map;
      }
    }

    dns::ddwaf_arr_obj ret;
    ret.make_array(merged.size(), memres);
    std::size_t i = 0;
    for (const auto &[id, map] : merged) {
      ret.at_unchecked(i++).shallow_copy_val_from(map);
    }

    return ret;
  }
};

class JsonParsedConfig {
 public:
  JsonParsedConfig(const std::string &content)
      : json_(nlohmann::json::parse(content)) {}

 protected:
  nlohmann::json json_;
};

class AsmFeaturesListener : public rc::ProductListener {
  class AppSecFeatures : public JsonParsedConfig {
   public:
    using JsonParsedConfig::JsonParsedConfig;

    bool asm_enabled() {
      try {
        return json_.at("/asm/enabled"_json_pointer).get<bool>();
      } catch (nlohmann::json::exception &) {
        return false;
      }
    }
  };

 public:
  AsmFeaturesListener(CurrentAppSecConfig &cur_appsec_cfg)
      : cur_appsec_cfg_{cur_appsec_cfg} {}

  void on_config_update(const rc::ParsedConfigKey &key,
                        const std::string &content) override {
    if (key.config_id() != "asm_features_activation"sv) {
      return;
    }

    AppSecFeatures features{content};
    bool new_state = features.asm_enabled();
    bool old_state = dns::library::active();
    if (new_state == old_state) {
      return;
    }

    dns::library::set_active(new_state);
  }

  void on_config_remove(const rc::ParsedConfigKey &key) override{
    return on_config_update(key, std::string{"{}"});
  };

  rc::CapabilitiesSet capabilities() const override {
    return rc::Capability::ASM_ACTIVATION;
  };

 private:
  CurrentAppSecConfig &cur_appsec_cfg_;
};

class AsmDDListener : public rc::ProductListener {
 public:
  AsmDDListener(CurrentAppSecConfig &cur_appsec_cfg,
                std::shared_ptr<dns::ddwaf_owned_map> default_config)
      : cur_appsec_cfg_{cur_appsec_cfg}, default_config_{default_config} {}

  void on_config_update(const rc::ParsedConfigKey &key,
                        const std::string &content) override {
    // convert content to rapidjson::Document:
    rapidjson::Document doc;
    rapidjson::ParseResult result = doc.Parse(content.c_str(), content.size());
    if (!result) {
      throw std::invalid_argument(
          "failed to parse remote config for asm_dd: " +
          std::string{rapidjson::GetParseError_En(result.Code())});
    }

    std::shared_ptr<dns::ddwaf_owned_map> new_config =
        std::make_shared<dns::ddwaf_owned_map>(dns::json_to_object(doc));

    cur_appsec_cfg_.set_dd_config(std::move(new_config));
  }

  void on_config_remove(const rc::ParsedConfigKey &key) override {
    cur_appsec_cfg_.set_dd_config(default_config_);
  }

  rc::CapabilitiesSet capabilities() const override {
    return {
      rc::Capability::ASM_DD_RULES,
      rc::Capability::ASM_IP_BLOCKING,
      rc::Capability::ASM_REQUEST_BLOCKING,
    };
  };

 private:
  CurrentAppSecConfig &cur_appsec_cfg_;
  std::shared_ptr<dns::ddwaf_owned_map> default_config_;
};

class AsmDataListener : public rc::ProductListener {
 public:
  AsmDataListener(CurrentAppSecConfig &cur_appsec_cfg)
      : cur_appsec_cfg_{cur_appsec_cfg} {}

  void on_config_update(const rc::ParsedConfigKey &key,
                        const std::string &content) override {
    rapidjson::Document doc;
    rapidjson::ParseResult result = doc.Parse(content.c_str(), content.size());
    if (!result) {
      throw std::invalid_argument(
          "failed to parse remote config for asm_data: " +
          std::string{rapidjson::GetParseError_En(result.Code())});
    }

    if (!doc.IsArray()) {
      throw std::invalid_argument("asm_data remote config not an array");
    }

    dns::ddwaf_owned_arr new_data{dns::json_to_object(doc)};
    cur_appsec_cfg_.asm_data_add_config(key, std::move(new_data));
  }

  void on_config_remove(const rc::ParsedConfigKey &key) override {
    cur_appsec_cfg_.asm_data_remove_config(key);
  }

  rc::CapabilitiesSet capabilities() const override { return {}; };

 private:
  CurrentAppSecConfig &cur_appsec_cfg_;
};

class AsmUserConfigListener : public rc::ProductListener {
 public:
  AsmUserConfigListener(CurrentAppSecConfig &cur_appsec_cfg)
      : cur_appsec_cfg_{cur_appsec_cfg} {}

  void on_config_update(const rc::ParsedConfigKey &key,
                        const std::string &content) override {

    rapidjson::Document doc;
    rapidjson::ParseResult result = doc.Parse(content.c_str(), content.size());
    if (!result) {
      throw std::invalid_argument(
          "failed to parse remote config for ASM product (user config): " +
          std::string{rapidjson::GetParseError_En(result.Code())});
    }

    AppSecUserConfig new_config{AppSecUserConfig::from_json(key, doc)};
    cur_appsec_cfg_.user_config_add_config(std::move(new_config));
  }

  void on_config_remove(const rc::ParsedConfigKey &key) override {
    cur_appsec_cfg_.user_config_remove_config(key);
  }

  rc::CapabilitiesSet capabilities() const override {
    return rc::Capability::ASM_CUSTOM_RULES;
  };

 private:
  CurrentAppSecConfig &cur_appsec_cfg_;
};

class AppSecConfigService {
  std::shared_ptr<dns::ddwaf_owned_map> default_config_;
  CurrentAppSecConfig current_config_;

public:
  AppSecConfigService(dns::ddwaf_owned_map default_config)
      : default_config_{std::make_shared<dns::ddwaf_owned_map>(
            std::move(default_config))} {
    current_config_.set_dd_config(this->default_config_);
  }

  void subscribe_to_remote_config(
      datadog::tracing::RemoteConfigurationManager &rcm,
      std::function<void(dns::ddwaf_map_obj)> accept_cfg_update) {
    // TODO: only subscribe this if not explicitly activated or deactivated
    subscribe_activation(rcm);

    // TODO: only subscribe if not given a configuration file
    subscribe_rules_and_data(rcm);

    rcm.add_config_end_listener([this, cb=std::move(accept_cfg_update)]{
      auto maybe_upd = current_config_.merged_update_config();
      if (maybe_upd) {
        cb(maybe_upd.value().get());
      }
    });
  }

private:
 void subscribe_activation(RemoteConfigurationManager &rcm) {
   rcm.add_listener(rc::Product::KnownProducts::ASM_FEATURES,
                    std::make_unique<AsmFeaturesListener>(current_config_));
 }

 void subscribe_rules_and_data(RemoteConfigurationManager &rcm) {
   rcm.add_listener(
       rc::Product::KnownProducts::ASM_DD,
       std::make_unique<AsmDDListener>(current_config_, default_config_));

   rcm.add_listener(rc::Product::KnownProducts::ASM_DATA,
                    std::make_unique<AsmDataListener>(current_config_));

   rcm.add_listener(rc::Product::KnownProducts::ASM,
                    std::make_unique<AsmUserConfigListener>(current_config_));
 }
};

}  // namespace

namespace datadog::nginx::security {

void register_with_remote_cfg(
    datadog::tracing::RemoteConfigurationManager &rcm,
    dns::ddwaf_owned_map default_config,
    std::function<void(dns::ddwaf_map_obj)> accept_config_update) {
  static bool called{};
  static AppSecConfigService app_sec_cfg{std::move(default_config)};

  if (called) {
    throw std::logic_error{"register_with_remote_cfg called more than once"};
  }
  called = true;

  app_sec_cfg.subscribe_to_remote_config(rcm, std::move(accept_config_update));
}
}  // namespace datadog::nginx::security
