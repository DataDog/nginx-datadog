#include "ddwaf_req.h"

#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/prettywriter.h>

#include <charconv>
#include <cppcodec/base64_rfc4648.hpp>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "blocking.h"
#include "compress.h"
#include "ddwaf_obj.h"
#include "string_util.h"

extern "C" {
#include <ngx_cycle.h>
#include <ngx_log.h>
}

using namespace std::literals;

namespace {

namespace dnsec = datadog::nginx::security;
using dnsec::BlockSpecification;
using dnsec::ddwaf_map_obj;
using dnsec::ddwaf_obj;
using LibddwafOwnedMap = dnsec::libddwaf_owned_ddwaf_obj<ddwaf_map_obj>;

class JsonWriter : public rapidjson::Writer<rapidjson::StringBuffer> {
  using rapidjson::Writer<rapidjson::StringBuffer>::Writer;

 public:
  // NOLINTNEXTLINE(readability-identifier-naming)
  bool ConstLiteralKey(std::string_view sv) {
    return String(sv.data(), sv.length(), false);
  }
};

void ddwaf_object_to_json(JsonWriter &w, const ddwaf_object &dobj);

// NOLINTNEXTLINE(misc-no-recursion)
void ddwaf_object_to_json(JsonWriter &w, const ddwaf_object &dobj) {
  switch (dobj.type) {
    case DDWAF_OBJ_MAP:
      w.StartObject();
      for (std::size_t i = 0; i < dobj.nbEntries; i++) {
        auto &&e = dobj.array[i];
        w.Key(e.parameterName, e.parameterNameLength, false);
        ddwaf_object_to_json(w, e);
      }
      w.EndObject(dobj.nbEntries);
      break;
    case DDWAF_OBJ_ARRAY:
      w.StartArray();
      for (std::size_t i = 0; i < dobj.nbEntries; i++) {
        auto &&e = dobj.array[i];
        ddwaf_object_to_json(w, e);
      }
      w.EndArray(dobj.nbEntries);
      break;
    case DDWAF_OBJ_STRING:
      w.String(dobj.stringValue, dobj.nbEntries, false);
      break;
    case DDWAF_OBJ_SIGNED:
      w.Int64(dobj.intValue);
      break;
    case DDWAF_OBJ_UNSIGNED:
      w.Uint64(dobj.uintValue);
      break;
    case DDWAF_OBJ_FLOAT:
      w.Double(dobj.f64);
      break;
    case DDWAF_OBJ_BOOL:
      w.Bool(dobj.boolean);
      break;
    case DDWAF_OBJ_INVALID:
    case DDWAF_OBJ_NULL:
      w.Null();
      break;
  }
}

class Action {
 public:
  enum class type : unsigned char {
    BLOCK_REQUEST,
    REDIRECT_REQUEST,
    GENERATE_STACK,
    GENERATE_SCHEMA,
    UNKNOWN,
  };

  Action(dnsec::ddwaf_map_obj action) : action_{action} {}

  auto type() const {
    auto key = action_.key();
    if (key == "block_request"sv) {
      return type::BLOCK_REQUEST;
    } else if (key == "redirect_request"sv) {
      return type::REDIRECT_REQUEST;
    } else if (key == "generate_stack"sv) {
      return type::GENERATE_STACK;
    } else if (key == "generate_schema"sv) {
      return type::GENERATE_SCHEMA;
    } else {
      return type::UNKNOWN;
    }
  }

  auto raw_type() const { return action_.key(); }

  int get_int_param(std::string_view k) const {
    ddwaf_obj v = action_.get(k);

    if (v.is_numeric()) {
      return v.numeric_val<int>();
    }

    if (!v.is_string()) {
      throw std::runtime_error{"expected numeric value for action parameter " +
                               std::string{k}};
    }

    // try to convert to number
    std::string_view const sv{v.string_val_unchecked()};
    int n;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), n);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
      return n;
    }
    throw std::runtime_error{"expected numeric value for action parameter " +
                             std::string{k} + ", got " + std::string{sv}};
  }

  std::string_view get_string_param(std::string_view k) const {
    ddwaf_obj v = action_.get(k);

    if (v.is_string()) {
      return v.string_val_unchecked();
    }

    throw std::runtime_error{"expected string value for action parameter " +
                             std::string{k}};
  }

 private:
  ddwaf_map_obj action_;
};

class ActionsResult {
 public:
  ActionsResult(ddwaf_map_obj actions) : actions_{actions} {}

  class Iterator {
   public:
    using difference_type = ddwaf_obj::nb_entries_t;      // NOLINT
    using value_type = Action;                            // NOLINT
    using pointer = value_type *;                         // NOLINT
    using reference = value_type &;                       // NOLINT
    using iterator_category = std::forward_iterator_tag;  // NOLINT

    Iterator(ddwaf_map_obj actions, ddwaf_obj::nb_entries_t i)
        : actions_{actions}, i_{i} {}
    Iterator &operator++() {
      ++i_;
      return *this;
    }

    bool operator!=(const Iterator &other) const { return i_ != other.i_; }

    Action operator*() const {
      return Action{actions_.at_unchecked<ddwaf_map_obj>(i_)};
    }

   private:
    ddwaf_map_obj actions_;
    ddwaf_obj::nb_entries_t i_;
  };

  Iterator begin() const { return Iterator{ddwaf_map_obj{actions_}, 0}; }
  Iterator end() const {
    return Iterator{ddwaf_map_obj{actions_}, actions_.size()};
  }

 private:
  ddwaf_map_obj actions_;
};

BlockSpecification create_block_request_action(const Action &action) {
  enum BlockSpecification::ContentType ct{
      BlockSpecification::ContentType::AUTO};
  int status = action.get_int_param("status_code"sv);

  std::string_view const ct_sv = action.get_string_param("type"sv);
  if (ct_sv == "auto"sv) {
    ct = BlockSpecification::ContentType::AUTO;
  } else if (ct_sv == "html"sv) {
    ct = BlockSpecification::ContentType::HTML;
  } else if (ct_sv == "json"sv) {
    ct = BlockSpecification::ContentType::JSON;
  } else if (ct_sv == "none"sv) {
    ct = BlockSpecification::ContentType::NONE;
  }

  return BlockSpecification{status, ct};
}

BlockSpecification create_redirect_request_action(const Action &action) {
  int status = action.get_int_param("status_code"sv);
  std::string_view const loc = action.get_string_param("location"sv);
  return {status, BlockSpecification::ContentType::NONE, loc};
}

std::optional<BlockSpecification> resolve_block_spec(
    const ActionsResult &actions) {
  for (Action act : actions) {
    auto type = act.type();

    if (type == Action::type::UNKNOWN) {
      continue;
    }

    if (type == Action::type::GENERATE_STACK ||
        type == Action::type::GENERATE_SCHEMA) {
      continue;
    }

    if (type == Action::type::BLOCK_REQUEST) {
      return {create_block_request_action(act)};
    }

    if (type == Action::type::REDIRECT_REQUEST) {
      return {create_redirect_request_action(act)};
    }
  }

  return std::nullopt;
}

}  // namespace

namespace datadog::nginx::security {

namespace {
constexpr int kMaxPlainSchemaAllowed = 260;
constexpr int kMaxSchemaSize = 25000;

template <typename Func>
bool handle_schema(ngx_log_t &log, const ddwaf_obj &obj, Func &&f) {
  ngx_str_t key = to_ngx_str(obj.key());

  if (!obj.key().starts_with("_dd.appsec.s."sv)) {
    return false;
  }

  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, &log, 0,
                 "ddwaf_req: handling attribute %V", &key);

  rapidjson::StringBuffer buffer;
  JsonWriter w(buffer);
  ddwaf_object_to_json(w, obj);

  std::string_view json = {buffer.GetString(), buffer.GetLength()};
  std::string b64;
  // compress + base64
  if (json.size() > kMaxPlainSchemaAllowed) {
    auto compressed = compress(json);
    if (!compressed) {
      ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                    "ddwaf_req: failed to compress attribute %V", &key);
      return true;
    }

    if (compressed->length() > kMaxSchemaSize * 3 / 4 + 1) {
      ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                    "ddwaf_req: compressed attribute %V is too large", &key);
      return true;
    }

    // then we need to base-64 encode it
    b64 = cppcodec::base64_rfc4648::encode(*compressed);
    if (b64.size() > kMaxSchemaSize) {
      ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                    "ddwaf_req: base-64 encoded attribute %V is too large",
                    key);
      return true;
    }

    json = b64;
  }

  std::invoke(std::forward<Func>(f), json);
  return true;
}

template <typename Func>
void handle_non_schema_attribute(ngx_log_t &log, const ddwaf_obj &obj,
                                 Func &&f) {
  ngx_str_t key = to_ngx_str(obj.key());

  if (obj.key().starts_with("_dd.appsec.s."sv)) {
    return;
  }

  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, &log, 0,
                 "ddwaf_req: handling non-schema attribute %V", &key);

  std::variant<std::string_view, double> v;
  if (obj.is_numeric()) {
    v = obj.numeric_val<double>();
  } else if (obj.is_string()) {
    v = obj.string_val_unchecked();
  } else {
    ngx_log_error(
        NGX_LOG_INFO, &log, 0,
        "ddwaf_req: non-schema attribute %V is not a string or number", &key);
    return;
  }

  std::invoke(std::forward<Func>(f), std::move(v));
}

}  // namespace

DdwafContext::DdwafContext(std::shared_ptr<OwnedDdwafHandle> &handle)
    : ctx_{nullptr} {
  if (!handle) {
    throw std::runtime_error{"invalid WAF handle"};
  }

  ddwaf_handle ddwaf_h = handle->get();
  ctx_ = ddwaf_context_init(ddwaf_h);
  if (!ctx_.resource) {
    throw std::runtime_error{"failed to initialize WAF context"};
  }
}

DdwafContext::WafRunResult DdwafContext::run(ngx_log_t &log,
                                             ddwaf_object &persistent_data) {
  LibddwafOwnedMap result{{}};
  DDWAF_RET_CODE const code =
      ddwaf_run(ctx_.resource, &persistent_data, nullptr, &result,
                Library::waf_timeout());

  WafRunResult waf_result;
  waf_result.ret_code = code;

  LibddwafOwnedMap &iresult = results_.emplace_back(std::move(result));

  std::optional<ddwaf_obj> maybe_keep = iresult.get_opt<ddwaf_obj>("keep"sv);
  if (maybe_keep && maybe_keep->is_bool()) {
    keep_ |= ddwaf_bool_obj{*maybe_keep};
  } else {
    // should not happen
    ngx_log_error(NGX_LOG_INFO, &log, 0,
                  "libddwaf did not provide a keep flag");
    keep_ = (code == DDWAF_MATCH);
  }

  std::optional<ddwaf_map_obj> maybe_attributes =
      iresult.get_opt<ddwaf_map_obj>("attributes"sv);
  if (maybe_attributes) {
    ddwaf_map_obj &attributes = *maybe_attributes;
    for (auto &&a : attributes) {
      bool is_schema = handle_schema(log, a, [&a, this](std::string_view json) {
        collected_tags_.emplace(a.key(), json);
      });
      if (!is_schema) {
        handle_non_schema_attribute(
            log, a, [&a, this](std::variant<std::string_view, double> v) {
              if (std::holds_alternative<std::string_view>(v)) {
                collected_tags_.emplace(a.key(), std::get<std::string_view>(v));
              } else {
                collected_metrics_.emplace(a.key(), std::get<double>(v));
              }
            });
      }
    }
  }

  if (code == DDWAF_MATCH) {
    std::optional<ddwaf_map_obj> actions_arr{
        iresult.get_opt<ddwaf_map_obj>("actions"sv)};
    if (actions_arr) {
      ActionsResult actions_res{*actions_arr};
      waf_result.block_spec = resolve_block_spec(actions_res);
    }
  }

  return waf_result;
}

bool DdwafContext::report_matches(
    const std::function<void(std::string_view)> &f) {
  if (results_.empty()) {
    return false;
  }

  std::vector<ddwaf_arr_obj *> events_arrs;
  for (LibddwafOwnedMap &result : results_) {
    std::optional<ddwaf_arr_obj> maybe_events =
        result.get_opt<ddwaf_arr_obj>("events");
    if (!maybe_events) {
      continue;
    }
    if (maybe_events->size() == 0) {
      continue;
    }
    events_arrs.push_back(&maybe_events.value());
  }

  if (events_arrs.empty()) {
    return false;
  }

  rapidjson::StringBuffer buffer;
  JsonWriter w(buffer);
  w.StartObject();
  w.ConstLiteralKey("triggers"sv);

  w.StartArray();
  for (ddwaf_arr_obj *events : events_arrs) {
    for (auto &&evt : *events) {
      ddwaf_object_to_json(w, evt);
    }
  }
  w.EndArray(results_.size());

  w.EndObject(1);
  w.Flush();

  std::string_view const json{buffer.GetString(), buffer.GetLength()};
  f(json);

  results_.clear();
  return true;
}

}  // namespace datadog::nginx::security
