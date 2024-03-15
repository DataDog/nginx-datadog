#include "waf_remote_cfg.h"

#include <datadog/remote_config.h>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>
#include <stdexcept>

#include "datadog/json_fwd.hpp"
#include "library.h"
#include "security/ddwaf_memres.h"
#include "security/ddwaf_obj.h"

namespace rc = datadog::tracing::remote_config;
namespace dns = datadog::nginx::security;
using namespace std::literals;

namespace {

struct DirtyStatus {
  bool rules;
  bool custom_rules;
  bool rule_overrides;
  bool actions;
  bool data;
  bool exclusions;

  DirtyStatus operator|(DirtyStatus oth) {
    return {rules || oth.rules, custom_rules || oth.custom_rules,
            rule_overrides || oth.rule_overrides, actions || oth.actions,
            data || oth.data, exclusions || oth.exclusions};
  }

  static DirtyStatus allDirty() {
    return {true, true, true, true, true, true};
  }

  bool is_any_dirty() const {
    return rules || custom_rules || rule_overrides || actions || data ||
           exclusions;
  }

  bool is_diry_for_ddwaf_update() const {
    return rules || custom_rules || rule_overrides || data || exclusions;
  }

  bool is_dirty_for_actions() const { return actions; }
};

class AppSecUserConfig {
  rc::ParsedConfigKey key_;
  dns::ddwaf_owned_map root_;
  dns::ddwaf_map_obj rules_overrides_;
  dns::ddwaf_map_obj actions_;
  dns::ddwaf_map_obj exclusions_;
  dns::ddwaf_map_obj custom_rules_;

public:
 AppSecUserConfig(rc::ParsedConfigKey key, dns::ddwaf_owned_map root)
     : key_{key}, root_{std::move(root)} {
    if (auto rules_overrides = root_.get().get_opt("rules_overrides"sv)) {
      rules_overrides_.shallow_copy_from(rules_overrides.value());
    }
    if (auto actions = root_.get().get_opt("actions"sv)) {
      actions_.shallow_copy_from(actions.value());
    }
    if (auto exclusions = root_.get().get_opt("exclusions"sv)) {
      exclusions_.shallow_copy_from(exclusions.value());
    }
    if (auto custom_rules = root_.get().get_opt("custom_rules"sv)) {
      custom_rules_.shallow_copy_from(custom_rules.value());
    }
 }

  const rc::ParsedConfigKey &key() const { return key_; }
  const dns::ddwaf_map_obj &rules_overrides() const { return rules_overrides_; }
  const dns::ddwaf_map_obj &actions() const { return actions_; }
  const dns::ddwaf_map_obj &exclusions() const { return exclusions_; }
  const dns::ddwaf_map_obj &custom_rules() const { return custom_rules_; }

  DirtyStatus dirty_effect() {
    // data not included here
    return {false,
            !custom_rules_.empty(),
            !rules_overrides_.empty(),
            !actions_.empty(),
            false,
            !exclusions_.empty()};
  }

  static AppSecUserConfig from_json(rc::ParsedConfigKey key,
                                    rapidjson::Document json) {
      if (!json.IsObject()) {
        throw std::invalid_argument("user config json not a map");
      }

      dns::ddwaf_owned_obj oo = dns::json_to_object(json);
      return AppSecUserConfig{key, dns::ddwaf_owned_map{std::move(oo)}};
  }
};

class CollectedUserConfigs {
  std::vector<AppSecUserConfig> user_configs_;

  public:
  DirtyStatus add_config(AppSecUserConfig new_config) {
    DirtyStatus removed_dirty = remove_config(new_config.key());
    DirtyStatus new_dirty =  new_config.dirty_effect();

    user_configs_.emplace_back(std::move(new_config));

    std::sort(user_configs_.begin(), user_configs_.end(),
              [](const AppSecUserConfig &lhs, const AppSecUserConfig &rhs) {
                return lhs.key().full_key() < rhs.key().full_key();
              });

    return removed_dirty | new_dirty;
  }

  const AppSecUserConfig &at(std::size_t index) const {
    return user_configs_.at(index);
  }

  std::size_t size() const { return user_configs_.size(); }

private:
  auto find_by_key(const rc::ParsedConfigKey &key) {
      return std::find_if(
          user_configs_.begin(), user_configs_.end(),
          [&key](const AppSecUserConfig &cfg) { return cfg.key() == key; });
  }

  DirtyStatus remove_config(const rc::ParsedConfigKey &key) {
    auto it = find_by_key(key);
    if (it == user_configs_.end()) {
      return DirtyStatus{}; // nothing dirty
    }
    
    DirtyStatus ret = it->dirty_effect();
    user_configs_.erase(it);
    return ret;
  }
};

class CollectedAsmData {
  std::unordered_map<rc::ParsedConfigKey, dns::ddwaf_owned_arr /*arr*/,
                     rc::ParsedConfigKey::Hash>
      data_;

  public:

  void add_config(const rc::ParsedConfigKey &key, dns::ddwaf_owned_arr new_config) {
    data_.emplace(key, std::move(new_config));
  }

  void remove_config(const rc::ParsedConfigKey &key) {
    data_.erase(key);
  }

  // returns [{id: "...", type: "...", data: [{...}, ...]}]
  // by merging the data value from all entries with the same id
  dns::ddwaf_arr_obj merged_data(dns::ddwaf_memres &memres) const {
    // first we need to group all the data by id
    auto grouped_entries = std::unordered_map<std::string_view,
      std::vector<dns::ddwaf_map_obj>>{};

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
    dns::ddwaf_arr_obj ret; // mixed ownership (data_ and memres)
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
    }

    return ret;
  }
};

class CurrentAppSecConfig {
  dns::ddwaf_owned_map dd_config_;
  CollectedUserConfigs user_configs_;
  CollectedAsmData asm_data_;
  DirtyStatus dirty_status_;

 public:
  void set_dd_config(dns::ddwaf_owned_map new_config) {
    static const rc::ParsedConfigKey KEY_BUNDLED_RULE_DATA{
        "no_org/NONE/none/bundled_rule_data"};
    dd_config_ = std::move(new_config);
    
    asm_data_.remove_config(KEY_BUNDLED_RULE_DATA);

    std::optional<dns::ddwaf_arr_obj> rules_data =
        dns::ddwaf_map_obj{new_config.get()}.get_opt<dns::ddwaf_arr_obj>(
            "rules_data"sv);
    if (rules_data) {
      dns::ddwaf_owned_arr rules_data_copy = dns::ddwaf_obj_clone(*rules_data);
      asm_data_.add_config(KEY_BUNDLED_RULE_DATA, std::move(rules_data_copy));
    }

    dirty_status_.rules = true;
  }

  // returns a mixed ownership object, with some static data,
  // some data owned by this object (indirectly), and newly allocated
  // data owned by the caller
  std::optional<dns::ddwaf_owned_map> merged_update_config() {
    if (!dirty_status_.is_any_dirty()) {
      return std::nullopt;
    }

    dns::ddwaf_owned_map ret;
    dns::ddwaf_map_obj &mo = ret.get().make_map(10, ret.memres()); // up to 10 entries
    dns::ddwaf_obj::nb_entries_t i = 0;
    if (dirty_status_.rules) {
      mo.get_entry_unchecked(i++)
          .set_key("metadata"sv)
          .shallow_copy_from(dd_config_.get()
                                 .get_opt<dns::ddwaf_map_obj>("metadata"sv)
                                 .value_or(dns::ddwaf_map_obj{}));
      mo.get_entry_unchecked(i++)
        .set_key("rules"sv)
        .shallow_copy_from(dd_config_.get()
                               .get_opt<dns::ddwaf_arr_obj>("rules"sv)
                               .value_or(dns::ddwaf_arr_obj{}));
      mo.get_entry_unchecked(i++)
        .set_key("processors"sv)
        .shallow_copy_from(dd_config_.get()
                               .get_opt<dns::ddwaf_arr_obj>("processors"sv)
                               .value_or(dns::ddwaf_arr_obj{}));
      mo.get_entry_unchecked(i++)
        .set_key("scannners"sv)
        .shallow_copy_from(dd_config_.get()
                               .get_opt<dns::ddwaf_arr_obj>("scanners"sv)
                               .value_or(dns::ddwaf_arr_obj{}));
    }

    if (dirty_status_.custom_rules) {

    }

    mo.nbEntries = i;

    return std::move(ret);
  }

private:
  dns::ddwaf_arr_obj get_merged_custom_rules(dns::ddwaf_memres &memres) {

  }

  dns::ddwaf_arr_obj merge_maps_by_id_keep_latest(
      const std::vector<dns::ddwaf_map_obj> &maps, dns::ddwaf_memres &memres) {
      std::unordered_map<std::string_view /*id*/, dns::ddwaf_map_obj> merged{};

      // for (const dns::ddwaf_map_obj &map : maps) {
      //   for (const dns::ddwaf_obj &entry : map.get<dns::ddwaf_arr_obj>("data"sv)) {
      //     dns::ddwaf_map_obj entry_map{entry};
      //     std::string_view id = entry_map.get<dns::ddwaf_str_obj>("id"sv).value();
      //     merged[id] = entry_map;
      //   }
      // }
  }
};

class JsonParsedConfig {
 public:
  JsonParsedConfig(const std::string &content)
      : json_(nlohmann::json::parse(content)) {}

 protected:
  nlohmann::json json_;
};

class AsmDDListener : public rc::ProductListener {
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
 void on_config_update(const rc::ParsedConfigKey &key,
                       const std::string &content) override {
    AppSecFeatures features{content};

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
