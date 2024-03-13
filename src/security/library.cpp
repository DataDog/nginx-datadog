#include "library.h"

#include <ddwaf.h>
#include <rapidjson/schema.h>
#include <unistd.h>

#include <optional>
#include <fstream>
#include <string_view>
#include <utility>

#include "ddwaf_obj.h"
#include "security/blocking.h"

using namespace std::literals;

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

template <typename T>
void json_to_object(ddwaf_object &object, T &doc) { // NOLINT(misc-no-recursion)
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

        std::string_view const key = kv.name.GetString();
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

  rule_file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  buffer.resize(rule_file.gcount());
  rule_file.close();

  rapidjson::Document document;
  rapidjson::ParseResult const result = document.Parse(buffer.data());
  if ((result == nullptr) || !document.IsObject()) {
    throw std::invalid_argument("invalid json rule");
  }

  ddwaf_object object;
  json_to_object(object, document);

  return object;
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

void ddwaf_log(DDWAF_LOG_LEVEL level, const char *function, const char *file,
               unsigned line, const char *message,
               uint64_t message_len) noexcept {
  int const log_level = ddwaf_log_level_to_nginx(level);
  ngx_log_error(log_level, ngx_cycle->log, 0, "ddwaf: %.*s at %s on %s:%d",
                static_cast<int>(message_len), message, function, file, line);
}
}  // namespace

namespace datadog::nginx::security {

waf_handle::waf_handle(ddwaf_object *ruleset) {
  handle_ = ddwaf_init(ruleset, &waf_config, nullptr);
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

std::shared_ptr<waf_handle> library::handle_{nullptr};
std::atomic<bool> library::active_{true};

void library::initialise_security_library(std::string_view file,
                                          std::string_view template_html,
                                          std::string_view template_json) {
  ddwaf_object ruleset;
  try {
    ruleset = read_rule_file(file);
  } catch (const std::exception &e) {
    throw std::runtime_error(std::string{"failed to read ruleset "} + "at " +
                             std::string{file} + ": " + e.what());
  }
  ddwaf_set_log_cb(ddwaf_log, DDWAF_LOG_DEBUG);
  library::handle_ = std::make_shared<waf_handle>(&ruleset);
  blocking_service::initialize(template_html, template_json);
}


bool library::update_ruleset(const ddwaf_map_obj &spec)
{
  std::shared_ptr<waf_handle> cur_h{get_handle_uncond()};

  ddwaf_handle new_h;

  if (!cur_h) {
    auto&& rules = spec.get_opt("rules"sv);
    if (!rules) {
      return false;
    }

    new_h = ddwaf_init(&spec, &waf_config, nullptr);
    if (!new_h) {
      return false;
    }
  } else {
    new_h = ddwaf_update(cur_h->get(), &spec, nullptr);
    if (new_h) {
      return false;
    }
  }

  auto &&actions = spec.get_opt<ddwaf_map_obj>("actions"sv);

  auto *wh = new waf_handle{new_h, actions.value_or(ddwaf_map_obj::empty())};
  std::atomic_store(&handle_, std::shared_ptr<waf_handle>{wh});
  return true;
}

}  // namespace datadog::nginx::security

std::vector<std::string_view> library::environment_variable_names() {
  return {// These environment variable names are taken from
          // `tracer_options.cpp` and `tracer.cpp` in the `dd-opentracing-cpp`
          // repository. I did `git grep '"DD_\w\+"' -- src/` in the
          // `dd-opentracing-cpp` repository.
          "DD_APPSEC_ENABLED", "DD_APPSEC_RULES"};
}

}  // namespace datadog
