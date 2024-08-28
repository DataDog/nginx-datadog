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

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include "ddwaf_memres.h"
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

  DirtyStatus &operator|=(DirtyStatus oth) { return (*this = *this | oth); }

  static DirtyStatus all_dirty() {
    return {true, true, true, true, true, true};
  }

  bool is_any_dirty() const {
    return rules || custom_rules || rules_override || actions || data ||
           exclusions;
  }
};

class AppSecUserConfig {
  ParsedConfigKey key_;
  dnsec::ddwaf_owned_map root_;
  // these are all arrays of maps: [{id: "...", ...}, ...]
  dnsec::ddwaf_arr_obj rules_override_;
  dnsec::ddwaf_arr_obj actions_;
  dnsec::ddwaf_arr_obj exclusions_;
  dnsec::ddwaf_arr_obj custom_rules_;

 public:
  AppSecUserConfig(ParsedConfigKey key, dnsec::ddwaf_owned_map root)
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

  const ParsedConfigKey &key() const { return key_; }
  const dnsec::ddwaf_arr_obj &rules_override() const { return rules_override_; }
  const dnsec::ddwaf_arr_obj &actions() const { return actions_; }
  const dnsec::ddwaf_arr_obj &exclusions() const { return exclusions_; }
  const dnsec::ddwaf_arr_obj &custom_rules() const { return custom_rules_; }

  DirtyStatus dirty_effect() {
    // data not included here
    return {false,
            !custom_rules_.empty(),
            !rules_override_.empty(),
            !actions_.empty(),
            false,
            !exclusions_.empty()};
  }

  static AppSecUserConfig from_json(ParsedConfigKey key,
                                    rapidjson::Document &json) {
    if (!json.IsObject()) {
      throw std::invalid_argument("user config json not a map");
    }

    dnsec::ddwaf_owned_obj oo =
        dnsec::json_to_object(json, dnsec::kConfigMaxDepth);

    return AppSecUserConfig{key, dnsec::ddwaf_owned_map{std::move(oo)}};
  }
};

class CollectedUserConfigs {
  std::vector<AppSecUserConfig> user_configs_;

  auto find_by_key(const ParsedConfigKey &key) {
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

  DirtyStatus remove_config(const ParsedConfigKey &key) {
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
  std::unordered_map<ParsedConfigKey, dnsec::ddwaf_owned_arr /*arr*/,
                     ParsedConfigKey::Hash>
      data_;

 public:
  void add_config(const ParsedConfigKey &key,
                  dnsec::ddwaf_owned_arr new_config) {
    data_.emplace(key, std::move(new_config));
  }

  void remove_config(const ParsedConfigKey &key) { data_.erase(key); }

  // returns [{id: "...", type: "...", data: [{...}, ...]}]
  // by merging the data value from all entries with the same id
  dnsec::ddwaf_arr_obj merged_data(dnsec::DdwafMemres &memres) const {
    // first we need to group all the data by id
    auto grouped_entries =
        std::unordered_map<std::string_view,
                           std::vector<dnsec::ddwaf_map_obj>>{};

    for (auto &&[key, owned_arr] : data_) {
      for (auto &&data_entry_obj : dnsec::ddwaf_arr_obj{owned_arr.get()}) {
        // data_entry is a map with id, type, and data
        dnsec::ddwaf_map_obj data_entry{data_entry_obj};
        std::string_view id =
            data_entry.get<dnsec::ddwaf_str_obj>("id"sv).value();
        grouped_entries[id].push_back(data_entry);
      }
    }

    // then we need to merge the data
    dnsec::ddwaf_arr_obj ret;  // mixed ownership (data_ and memres)
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
      dnsec::ddwaf_obj &out = ret.at_unchecked(i++);

      dnsec::ddwaf_map_obj &merged_map = out.make_map(3, memres);
      // id is a string_view backed by one of the data etnries
      merged_map.at_unchecked(0).set_key("id"sv).make_string(id);

      // check if the type is always the same for this id
      std::string_view first_type =
          vec.at(0).get<dnsec::ddwaf_str_obj>("type"sv).value();
      for (std::size_t j = 1; j < vec.size(); ++j) {
        if (vec.at(j).get<dnsec::ddwaf_str_obj>("type"sv).value() !=
            first_type) {
          throw std::invalid_argument(
              "type is not the same for all data entries with id=" +
              std::string{id});
        }
      }
      merged_map.at_unchecked(1).set_key("type"sv).make_string(first_type);

      // finally, the merged "data" key
      std::size_t total_data_entries = 0;
      for (dnsec::ddwaf_map_obj &obj : vec) {
        total_data_entries += obj.get<dnsec::ddwaf_arr_obj>("data"sv).size();
      }
      dnsec::ddwaf_arr_obj &merged_data =
          merged_map.at_unchecked(2).set_key("data"sv).make_array(
              total_data_entries, memres);

      std::size_t k = 0;
      for (dnsec::ddwaf_map_obj &obj : vec) {
        dnsec::ddwaf_arr_obj cur_data = obj.get<dnsec::ddwaf_arr_obj>("data"sv);
        std::memcpy(&merged_data.at_unchecked<ddwaf_object>(k), cur_data.array,
                    cur_data.size() * sizeof(dnsec::ddwaf_obj));
        k += cur_data.size();
      }
      assert(total_data_entries == k);
    }
    assert(num_ids == i);

    return ret;
  }
};

class CurrentAppSecConfig {
  std::shared_ptr<dnsec::ddwaf_owned_map> dd_config_;
  CollectedUserConfigs user_configs_;
  CollectedAsmData asm_data_;
  DirtyStatus dirty_status_;

 public:
  void set_dd_config(std::shared_ptr<dnsec::ddwaf_owned_map> new_config) {
    static const ParsedConfigKey key_bundled_rule_data{
        "datadog/0/NONE/none/bundled_rule_data"};

    assert(new_config);

    dd_config_ = std::move(new_config);

    asm_data_.remove_config(key_bundled_rule_data);

    std::optional<dnsec::ddwaf_arr_obj> rules_data =
        dd_config().get_opt<dnsec::ddwaf_arr_obj>("rules_data"sv);
    if (rules_data) {
      dnsec::ddwaf_owned_arr rules_data_copy =
          dnsec::ddwaf_obj_clone(*rules_data);
      asm_data_.add_config(key_bundled_rule_data, std::move(rules_data_copy));
    }

    dirty_status_.rules = true;
  }

  void asm_data_add_config(const ParsedConfigKey &key,
                           dnsec::ddwaf_owned_arr new_config) {
    asm_data_.add_config(key, std::move(new_config));
    dirty_status_.data = true;
  }

  void asm_data_remove_config(const ParsedConfigKey &key) {
    asm_data_.remove_config(key);
    dirty_status_.data = true;
  }

  void user_config_add_config(AppSecUserConfig new_config) {
    dirty_status_ |= user_configs_.add_config(std::move(new_config));
  }

  void user_config_remove_config(const ParsedConfigKey &key) {
    dirty_status_ |= user_configs_.remove_config(key);
  }

  // Main method
  // returns a mixed ownership object, with some static data,
  // some data owned by this object (indirectly), and newly allocated
  // data owned by the caller
  std::optional<dnsec::ddwaf_owned_map> merged_update_config() {
    if (!dirty_status_.is_any_dirty()) {
      return std::nullopt;
    }

    dnsec::ddwaf_owned_map ret;
    dnsec::ddwaf_map_obj &mo =
        ret.get().make_map(10, ret.memres());  // up to 10 entries
    dnsec::ddwaf_obj::nb_entries_t i = 0;
    if (dirty_status_.rules) {
      mo.at_unchecked(i++)
          .set_key("metadata"sv)
          .shallow_copy_val_from(
              dd_config()
                  .get_opt<dnsec::ddwaf_map_obj>("metadata"sv)
                  .value_or(dnsec::ddwaf_map_obj{}));
      mo.at_unchecked(i++).set_key("rules"sv).shallow_copy_val_from(
          dd_config().get_opt<dnsec::ddwaf_arr_obj>("rules"sv).value_or(
              dnsec::ddwaf_arr_obj{}));
      mo.at_unchecked(i++)
          .set_key("processors"sv)
          .shallow_copy_val_from(
              dd_config()
                  .get_opt<dnsec::ddwaf_arr_obj>("processors"sv)
                  .value_or(dnsec::ddwaf_arr_obj{}));
      mo.at_unchecked(i++)
          .set_key("scannners"sv)
          .shallow_copy_val_from(
              dd_config()
                  .get_opt<dnsec::ddwaf_arr_obj>("scanners"sv)
                  .value_or(dnsec::ddwaf_arr_obj{}));
    }

    if (dirty_status_.custom_rules) {
      mo.at_unchecked(i++)
          .set_key("custom_rules"sv)
          .shallow_copy_val_from(get_merged_custom_rules(ret.memres()));
    }

    if (dirty_status_.exclusions || dirty_status_.rules) {
      mo.at_unchecked(i++)
          .set_key("exclusions"sv)
          .shallow_copy_val_from(get_merged_exclusions(ret.memres()));
    }

    if (dirty_status_.rules_override || dirty_status_.rules) {
      mo.at_unchecked(i++)
          .set_key("rules_override"sv)
          .shallow_copy_val_from(get_merged_rule_overrides(ret.memres()));
    }

    if (dirty_status_.data || dirty_status_.rules) {
      mo.at_unchecked(i++)
          .set_key("rules_data"sv)
          .shallow_copy_val_from(asm_data_.merged_data(ret.memres()));
    }

    if (dirty_status_.actions || dirty_status_.rules) {
      mo.at_unchecked(i++)
          .set_key("actions"sv)
          .shallow_copy_val_from(get_merged_actions(ret.memres()));
    }

    mo.at_unchecked(i++)
        .set_key("version"sv)
        .shallow_copy_val_from(
            dd_config()
                .get_opt<dnsec::ddwaf_str_obj>("version"sv)
                .value_or(dnsec::ddwaf_str_obj{}.make_string("2.2"sv)));

    mo.nbEntries = i;

    dirty_status_ = DirtyStatus{};  // cleanse

    return std::move(ret);
  }

 private:
  const dnsec::ddwaf_map_obj &dd_config() { return dd_config_.get()->get(); }

  dnsec::ddwaf_arr_obj get_merged_custom_rules(dnsec::DdwafMemres &memres) {
    std::vector<dnsec::ddwaf_arr_obj> arr_of_maps(user_configs_.size());
    for (auto &&uc : user_configs_) {
      arr_of_maps.push_back(uc.custom_rules());
    }

    return merge_maps_by_id_keep_latest(arr_of_maps, memres);
  }

  dnsec::ddwaf_arr_obj get_merged_exclusions(dnsec::DdwafMemres &memres) {
    std::vector<dnsec::ddwaf_arr_obj> arr_of_maps(user_configs_.size() + 1);

    dnsec::ddwaf_arr_obj dd_rules_exclusions =
        dd_config()
            .get_opt<dnsec::ddwaf_arr_obj>("exclusions"sv)
            .value_or(dnsec::ddwaf_arr_obj{});

    arr_of_maps.push_back(dd_rules_exclusions);
    for (auto &&uc : user_configs_) {
      arr_of_maps.push_back(uc.exclusions());
    }

    return merge_maps_by_id_keep_latest(arr_of_maps, memres);
  }

  // does not include default actions
  dnsec::ddwaf_arr_obj get_merged_actions(dnsec::DdwafMemres &memres) {
    std::vector<dnsec::ddwaf_arr_obj> arr_of_maps(user_configs_.size() + 1);

    dnsec::ddwaf_arr_obj dd_actions =
        dd_config()
            .get_opt<dnsec::ddwaf_arr_obj>("actions"sv)
            .value_or(dnsec::ddwaf_arr_obj{});

    arr_of_maps.push_back(dd_actions);
    for (auto &&uc : user_configs_) {
      arr_of_maps.push_back(uc.actions());
    }

    return merge_maps_by_id_keep_latest(arr_of_maps, memres);
  }

  dnsec::ddwaf_arr_obj get_merged_rule_overrides(dnsec::DdwafMemres &memres) {
    std::vector<dnsec::ddwaf_arr_obj> arr_of_maps(user_configs_.size() + 1);

    dnsec::ddwaf_arr_obj dd_rules_override =
        dd_config()
            .get_opt<dnsec::ddwaf_arr_obj>("rules_override"sv)
            .value_or(dnsec::ddwaf_arr_obj{});

    arr_of_maps.push_back(dd_rules_override);
    for (auto &&uc : user_configs_) {
      arr_of_maps.push_back(uc.rules_override());
    }

    // plain merge; overrides have no id
    std::size_t total_count = 0;
    for (auto &&arr : arr_of_maps) {
      total_count += arr.size();
    }

    dnsec::ddwaf_arr_obj ret;
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

  dnsec::ddwaf_arr_obj merge_maps_by_id_keep_latest(
      const std::vector<dnsec::ddwaf_arr_obj> &arr_of_maps,
      dnsec::DdwafMemres &memres) {
    std::unordered_map<std::string_view /*id*/, dnsec::ddwaf_map_obj> merged{};

    for (const dnsec::ddwaf_arr_obj &arr : arr_of_maps) {
      for (const dnsec::ddwaf_obj &obj : arr) {
        dnsec::ddwaf_map_obj map{obj};
        std::string_view id = map.get<dnsec::ddwaf_str_obj>("id"sv).value();
        merged[id] = map;
      }
    }

    dnsec::ddwaf_arr_obj ret;
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

    std::shared_ptr<dnsec::ddwaf_owned_map> new_config =
        std::make_shared<dnsec::ddwaf_owned_map>(
            dnsec::json_to_object(doc, dnsec::kConfigMaxDepth));

    cur_appsec_cfg_.set_dd_config(std::move(new_config));
  }

  void on_revert_impl(const ParsedConfigKey &key) {
    cur_appsec_cfg_.set_dd_config(default_config_);
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

    if (doc.HasMember("rules_data")) {
      auto &rules_data = doc["rules_data"];
      if (!rules_data.IsArray()) {
        throw std::invalid_argument("rules_data is not an array");
      }
      logger_.log_debug([&key, &rules_data](std::ostream &oss) {
        oss << "rules_data: key(" << key.config_id() << ") "
            << "size(" << rules_data.Size() << ")";
      });

      dnsec::ddwaf_owned_arr new_data{
          dnsec::json_to_object(rules_data, dnsec::kConfigMaxDepth)};
      cur_appsec_cfg_.asm_data_add_config(key, std::move(new_data));
    } else {
      // no data
      dnsec::ddwaf_owned_arr new_data{};
      logger_.log_debug([&key](std::ostream &oss) {
        oss << "rules_data: key(" << key.config_id() << ") "
            << "empty data";
      });
      cur_appsec_cfg_.asm_data_add_config(key, std::move(new_data));
    }
  }

  void on_revert_impl(const ParsedConfigKey &key) {
    cur_appsec_cfg_.asm_data_remove_config(key);
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

    AppSecUserConfig new_config{AppSecUserConfig::from_json(key, doc)};
    cur_appsec_cfg_.user_config_add_config(std::move(new_config));
  }

  void on_revert_impl(const ParsedConfigKey &key) {
    cur_appsec_cfg_.user_config_remove_config(key);
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
        logger_{std::move(logger)} {
    current_config_.set_dd_config(this->default_config_);
  }

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
            std::optional<dnsec::ddwaf_owned_map> maybe_upd =
                current_config_.merged_update_config();
            if (maybe_upd) {
              auto &&upd = *maybe_upd;
              dnsec::Library::update_ruleset(upd.get());
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
