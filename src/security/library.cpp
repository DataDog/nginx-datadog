#include "library.h"

#include <ddwaf.h>
#include <rapidjson/schema.h>
#include <unistd.h>

#include <fstream>
#include <string_view>
#include <utility>

#include "ddwaf_obj.h"
#include "security/blocking.h"

namespace {

template <typename T>
void json_to_object(ddwaf_object &object, T &doc) {
  switch (doc.GetType()) {
    case rapidjson::kFalseType:
      ddwaf_object_stringl(&object, "false", sizeof("false") - 1);
      break;
    case rapidjson::kTrueType:
      ddwaf_object_stringl(&object, "true", sizeof("true") - 1);
      break;
    case rapidjson::kObjectType: {
      ddwaf_object_map(&object);
      for (auto &kv : doc.GetObject()) {
        ddwaf_object element;
        json_to_object(element, kv.value);

        std::string_view key = kv.name.GetString();
        ddwaf_object_map_addl(&object, key.data(), key.length(), &element);
      }
      break;
    }
    case rapidjson::kArrayType: {
      ddwaf_object_array(&object);
      for (auto &v : doc.GetArray()) {
        ddwaf_object element;
        json_to_object(element, v);

        ddwaf_object_array_add(&object, &element);
      }
      break;
    }
    case rapidjson::kStringType: {
      ddwaf_object_stringl(&object, doc.GetString(), doc.GetStringLength());
      break;
    }
    case rapidjson::kNumberType: {
      if (doc.IsInt64()) {
        ddwaf_object_signed(&object, doc.GetInt64());
      } else if (doc.IsUint64()) {
        ddwaf_object_unsigned(&object, doc.GetUint64());
      }
      break;
    }
    case rapidjson::kNullType:
    default:
      ddwaf_object_invalid(&object);
      break;
  }
}

ddwaf_object read_rule_file(std::string_view filename) {
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

  rule_file.read(&buffer[0], static_cast<std::streamsize>(buffer.size()));
  buffer.resize(rule_file.gcount());
  rule_file.close();

  rapidjson::Document document;
  rapidjson::ParseResult result = document.Parse(buffer.data());
  if ((result == nullptr) || !document.IsObject()) {
    throw std::invalid_argument("invalid json rule");
  }

  ddwaf_object object;
  json_to_object(object, document);

  return object;
}
}  // namespace

namespace datadog {
namespace nginx {
namespace security {

waf_handle::waf_handle(ddwaf_object *ruleset) {
  static constexpr ddwaf_config config{
      .limits =
          {
              .max_container_size = 150,
              .max_container_depth = 10,
              .max_string_length = 4096,
          },
      .free_fn = nullptr,
  };
  handle_ = ddwaf_init(ruleset, &config, nullptr);
  if (handle_ == nullptr) {
    throw std::runtime_error("unknown error");
  }
  action_info_map_ = extract_actions(*ruleset);
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

  ddwaf_map_obj root{ruleset};
  std::optional<ddwaf_arr_obj> actions = root.get_opt<ddwaf_arr_obj>("actions");
  if (!actions) {
    return default_actions();
  }

  action_info_map_t action_info_map{default_actions()};
  for (auto &&v : *actions) {
    ddwaf_map_obj action_spec{v};

    std::string_view id = ddwaf_str_obj{action_spec.get<ddwaf_str_obj>("id")}.value();
    std::string_view type = ddwaf_str_obj{action_spec.get<ddwaf_str_obj>("type")}.value();
    std::optional<ddwaf_map_obj> parameters =
        action_spec.get_opt<ddwaf_map_obj>("parameters");

    std::map<std::string, action_info::str_or_int, std::less<>> parameters_map;
    if (parameters) {
      for (auto &&[pkey, pvalue] : *parameters) {
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

waf_handle::~waf_handle() {
  if (handle_ != nullptr) {
    ddwaf_destroy(handle_);
  }
}

std::shared_ptr<waf_handle> library::handle_{nullptr};

void library::initialise_security_library(std::string_view file,
                                          std::string_view template_html,
                                          std::string_view template_json) {
  auto ruleset = read_rule_file(file);
  library::handle_ = std::make_shared<waf_handle>(&ruleset);
  blocking_service::initialize(template_html,
                               template_json);
}

std::vector<std::string_view> library::environment_variable_names() {
  return {// These environment variable names are taken from
          // `tracer_options.cpp` and `tracer.cpp` in the `dd-opentracing-cpp`
          // repository. I did `git grep '"DD_\w\+"' -- src/` in the
          // `dd-opentracing-cpp` repository.
          "DD_APPSEC_ENABLED", "DD_APPSEC_RULES"};
}

}  // namespace security
}  // namespace nginx
}  // namespace datadog
