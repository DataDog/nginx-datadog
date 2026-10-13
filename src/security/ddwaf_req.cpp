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
void ddwaf_map_to_json(JsonWriter &w, const dnsec::ddwaf_map_obj &map);
void ddwaf_array_to_json(JsonWriter &w, const dnsec::ddwaf_arr_obj &arr);

// NOLINTNEXTLINE(misc-no-recursion)
void ddwaf_object_to_json(JsonWriter &w, const ddwaf_object &dobj) {
  const ddwaf_obj &obj = ddwaf_obj::from(dobj);
  if (obj.is_map()) {
    ddwaf_map_to_json(w, ddwaf_obj::cast_unchecked<dnsec::ddwaf_map_obj>(obj));
  } else if (obj.is_array()) {
    ddwaf_array_to_json(w,
                        ddwaf_obj::cast_unchecked<dnsec::ddwaf_arr_obj>(obj));
  } else if (obj.is_string()) {
    std::string_view const sv = obj.string_val_unchecked();
    // rapidjson asserts on a null pointer
    w.String(sv.data() != nullptr ? sv.data() : "", sv.size(), false);
  } else if (dobj.type == DDWAF_OBJ_SIGNED) {
    w.Int64(dobj.via.i64.val);
  } else if (dobj.type == DDWAF_OBJ_UNSIGNED) {
    w.Uint64(dobj.via.u64.val);
  } else if (dobj.type == DDWAF_OBJ_FLOAT) {
    w.Double(dobj.via.f64.val);
  } else if (dobj.type == DDWAF_OBJ_BOOL) {
    w.Bool(dobj.via.b8.val);
  } else {
    // invalid, null or unknown
    w.Null();
  }
}

// NOLINTNEXTLINE(misc-no-recursion)
void ddwaf_map_to_json(JsonWriter &w, const dnsec::ddwaf_map_obj &map) {
  w.StartObject();
  for (const dnsec::ddwaf_kv_obj &entry : map) {
    std::string_view const key = entry.key();
    w.Key(key.data() != nullptr ? key.data() : "", key.size(), false);
    ddwaf_object_to_json(w, entry.val.raw());
  }
  w.EndObject(map.size());
}

// NOLINTNEXTLINE(misc-no-recursion)
void ddwaf_array_to_json(JsonWriter &w, const dnsec::ddwaf_arr_obj &arr) {
  w.StartArray();
  for (const ddwaf_obj &elem : arr) {
    ddwaf_object_to_json(w, elem.raw());
  }
  w.EndArray(arr.size());
}

// Holds references into the WAF result. Never copy values out of it: small
// strings live inside the object.
class Action {
 public:
  enum class type : unsigned char {
    BLOCK_REQUEST,
    REDIRECT_REQUEST,
    GENERATE_STACK,
    GENERATE_SCHEMA,
    UNKNOWN,
  };

  explicit Action(const dnsec::ddwaf_kv_obj &action) : action_{action} {}

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
    const ddwaf_obj &v = params().get(k);

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
    const ddwaf_obj &v = params().get(k);

    if (v.is_string()) {
      return v.string_val_unchecked();
    }

    throw std::runtime_error{"expected string value for action parameter " +
                             std::string{k}};
  }

 private:
  const ddwaf_map_obj &params() const {
    return ddwaf_obj::cast<ddwaf_map_obj>(action_.val);
  }

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
  const dnsec::ddwaf_kv_obj &action_;
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
    const ddwaf_map_obj &actions) {
  for (const dnsec::ddwaf_kv_obj &entry : actions) {
    Action const act{entry};
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
bool handle_schema(ngx_log_t &log, const ddwaf_kv_obj &attr, Func &&f) {
  ngx_str_t key = to_ngx_str(attr.key());

  if (!attr.key().starts_with("_dd.appsec.s."sv)) {
    return false;
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, &log, 0, "ddwaf_req: handling attribute %V",
                &key);

  rapidjson::StringBuffer buffer;
  JsonWriter w(buffer);
  ddwaf_object_to_json(w, attr.val.raw());

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
                    &key);
      return true;
    }

    json = b64;
  }

  std::invoke(std::forward<Func>(f), json);
  return true;
}

template <typename Func>
void handle_non_schema_attribute(ngx_log_t &log, const ddwaf_kv_obj &attr,
                                 Func &&f) {
  ngx_str_t key = to_ngx_str(attr.key());
  const ddwaf_obj &obj = attr.val;

  if (attr.key().starts_with("_dd.appsec.s."sv)) {
    return;
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, &log, 0,
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
  ctx_ = ddwaf_context_init(ddwaf_h, ddwaf_get_default_allocator());
  if (!ctx_.resource) {
    throw std::runtime_error{"failed to initialize WAF context"};
  }
}

DdwafContext::WafRunResult DdwafContext::run(ngx_log_t &log,
                                             ddwaf_object &persistent_data) {
  // The input is borrowed (null allocator): libddwaf never frees it.
  LibddwafOwnedMap result;
  DDWAF_RET_CODE const code =
      ddwaf_context_eval(ctx_.resource, &persistent_data, nullptr, &result.o,
                         Library::waf_timeout());

  WafRunResult waf_result;
  waf_result.ret_code = code;

  if (code < 0 || !result.is_map()) {
    ngx_log_error(NGX_LOG_ERR, &log, 0, "libddwaf evaluation failed: %d",
                  static_cast<int>(code));
    return waf_result;
  }

  // moving the map header is safe: its entries are on the heap
  const LibddwafOwnedMap &iresult = results_.emplace_back(std::move(result));

  const ddwaf_obj *maybe_keep = iresult.get_opt<ddwaf_obj>("keep"sv);
  if (maybe_keep && maybe_keep->is_bool()) {
    keep_ |= static_cast<bool>(ddwaf_obj::cast<ddwaf_bool_obj>(*maybe_keep));
  } else {
    // should not happen
    ngx_log_error(NGX_LOG_INFO, &log, 0,
                  "libddwaf did not provide a keep flag");
    keep_ = (code == DDWAF_MATCH);
  }

  if (const auto *attributes = iresult.get_opt<ddwaf_map_obj>("attributes"sv)) {
    collect_attributes(log, *attributes);
  }

  if (code == DDWAF_MATCH) {
    if (const auto *actions = iresult.get_opt<ddwaf_map_obj>("actions"sv)) {
      waf_result.block_spec = resolve_block_spec(*actions);
    }
  }

  return waf_result;
}

// string_views point into results_
void DdwafContext::collect_attributes(ngx_log_t &log,
                                      const ddwaf_map_obj &attributes) {
  for (const ddwaf_kv_obj &a : attributes) {
    bool is_schema = handle_schema(log, a, [&a, this](std::string_view json) {
      collected_tags_.emplace(a.key(), json);
    });
    if (is_schema) {
      continue;
    }
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

bool DdwafContext::has_matches() const {
  for (const libddwaf_owned_ddwaf_obj<ddwaf_map_obj> &result : results_) {
    const ddwaf_arr_obj *maybe_events = result.get_opt<ddwaf_arr_obj>("events");
    if (maybe_events && maybe_events->size() > 0) {
      return true;
    }
  }
  return false;
}

bool DdwafContext::report_matches(
    const std::function<void(std::string_view)> &f) {
  if (results_.empty()) {
    return false;
  }

  std::vector<const ddwaf_arr_obj *> events_arrs;
  for (LibddwafOwnedMap &result : results_) {
    const ddwaf_arr_obj *maybe_events = result.get_opt<ddwaf_arr_obj>("events");
    if (!maybe_events) {
      continue;
    }
    if (maybe_events->size() == 0) {
      continue;
    }
    events_arrs.push_back(maybe_events);
  }

  if (events_arrs.empty()) {
    return false;
  }

  rapidjson::StringBuffer buffer;
  JsonWriter w(buffer);
  w.StartObject();
  w.ConstLiteralKey("triggers"sv);

  w.StartArray();
  for (const ddwaf_arr_obj *events : events_arrs) {
    for (const ddwaf_obj &evt : *events) {
      ddwaf_object_to_json(w, evt.raw());
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
