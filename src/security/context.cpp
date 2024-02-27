#include "context.h"

#include <charconv>
#include <optional>
#include <utility>
#include <variant>

#include "../datadog_conf.h"
#include "../ngx_http_datadog_module.h"
#include "collection.h"
#include "datadog/span_data.h"
#include "datadog/trace_segment.h"
#include "ddwaf_obj.h"
#include "library.h"
#include "security/blocking.h"
#include "tracing_library.h"

extern "C" {
#include <ngx_hash.h>
#include <ngx_http.h>
#include <ngx_regex.h>
#include <ngx_string.h>
#include <ngx_thread_pool.h>
}

#include <ddwaf.h>
#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/prettywriter.h>

#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>

using namespace std::literals;

namespace {

namespace dns = datadog::nginx::security;

class JsonWriter : public rapidjson::Writer<rapidjson::StringBuffer> {
  using rapidjson::Writer<rapidjson::StringBuffer>::Writer;

 public:
  bool ConstLiteralKey(std::string_view sv) {
    return String(sv.data(), sv.length(), false);
  }
};

void ddwaf_object_to_json(JsonWriter &w, const ddwaf_object &dobj);

void report_match(const ngx_http_request_t &req, dd::TraceSegment &seg,
                  dd::SpanData &span,
                  std::vector<dns::owned_ddwaf_result> &results) {
  static constexpr std::string_view APPSEC_EVENT{"appsec.event"};
  static constexpr std::string_view APPSEC_JSON{"_dd.appsec.json"};

  if (results.empty()) {
    return;
  }

  seg.override_sampling_priority(2);  // USER-KEEP
  span.tags[std::string{APPSEC_EVENT}] = "true";

  rapidjson::StringBuffer buffer;
  JsonWriter w(buffer);
  w.StartObject();
  w.ConstLiteralKey("triggers"sv);

  w.StartArray();
  for (auto &&result : results) {
    auto &&events = (*result).events;
    for (std::size_t i = 0; i < events.nbEntries; i++) {
      auto &&evt = events.array[i];
      ddwaf_object_to_json(w, evt);
    }
  }
  w.EndArray(results.size());

  w.EndObject(1);
  w.Flush();

  const char *json_str = buffer.GetString();
  std::size_t json_str_len = buffer.GetLength();

  ngx_log_error(NGX_LOG_WARN, req.connection->log, 0, "appsec event: %.*s",
                static_cast<int>(json_str_len), json_str);

  span.tags[std::string{APPSEC_JSON}] = std::string{json_str, json_str_len};
}

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

template <typename Callable,
          typename Ret = decltype(std::declval<Callable>()())>
auto catch_exceptions(std::string_view name, const ngx_http_request_t &req,
                      Callable &&f) noexcept {
  try {
    return f();
  } catch (const std::exception &e) {
    ngx_log_error(NGX_LOG_ERR, req.connection->log, 0,
                  "security_context::%.*s: %s", static_cast<int>(name.size()),
                  name.data(), e.what());
  } catch (...) {
    ngx_log_error(NGX_LOG_ERR, req.connection->log, 0,
                  "security_context::%.*s: unknown exception",
                  static_cast<int>(name.size()), name.data());
  }
  if constexpr (!std::is_same_v<Ret, void>) {
    return Ret{};
  }
}

dd::SpanData &get_span_data(dd::Span &s) {
  auto *p =
      reinterpret_cast<char *>(&s) + sizeof(std::shared_ptr<dd::TraceSegment>);
  dd::SpanData *span_data;
  std::memcpy(&span_data, p, sizeof(span_data));  // NOLINT
  if (span_data == nullptr) {
    throw std::runtime_error("get_span_data failed: could not find span data");
  }
  return *span_data;
}
}  // namespace

namespace datadog {
namespace nginx {
namespace security {

context::context() : stage_{new std::atomic<stage>{}} {
  std::shared_ptr<waf_handle> handle = library::get_handle();
  if (!handle) {
    return;
  }

  ddwaf_handle ddwaf_h = handle->get();
  ctx_ = ddwaf_context_init(ddwaf_h);
  waf_handle_ = std::move(handle);

  stage_->store(stage::start, std::memory_order_release);
}

struct pol_task_ctx {
  static void handler(void *self, ngx_log_t *log) noexcept {
    static_cast<pol_task_ctx *>(self)->handle(log);
  }

  // runs on the thread pool
  void handle(ngx_log_t *log) noexcept {
    try {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "before task main: %p", &req_);
      block_spec_ = f_(log);
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "after task main: %p", &req_);
    } catch (std::exception &e) {
      ngx_log_error(NGX_LOG_ERR, log, 0, "task failed: %s", e.what());
    } catch (...) {
      ngx_log_error(NGX_LOG_ERR, log, 0, "task failed: unknown failure");
    }
  }

  // runs on the main thread
  static void completion_handler(ngx_event_t *evt) noexcept {
    pol_task_ctx *self = static_cast<pol_task_ctx *>(evt->data);
    static_cast<pol_task_ctx *>(self)->complete();
  }

  void complete() noexcept {
    if (block_spec_) {
      auto *service = blocking_service::get_instance();
      assert(service != nullptr);
      try {
        service->block(*block_spec_, req_);
      } catch (const std::exception &e) {
        ngx_log_error(NGX_LOG_ERR, req_.connection->log, 0,
                      "failed to block request: %s", e.what());
        ngx_http_finalize_request(&req_, NGX_DONE);
      }
    } else {
      req_.phase_handler++;  // move past us
      ngx_http_core_run_phases(&req_);
    }

    this->~pol_task_ctx();
  }

  ngx_http_request_t &req_;
  std::function<std::optional<block_spec>(ngx_log_t *)> f_;
  std::optional<block_spec> block_spec_;
};

bool context::on_request_start(ngx_http_request_t &request,
                               dd::Span &span) noexcept {
  return catch_exceptions("on_request_start", request, [&]() {
    return context::do_on_request_start(request, span);
  });
}

bool context::do_on_request_start(ngx_http_request_t &request, dd::Span &span) {
  if (ctx_.resource == nullptr) {
    return false;
  }

  auto *conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));

  if (conf->waf_pool == nullptr) {
    return false;
  }

  ngx_thread_task_t *task =
      ngx_thread_task_alloc(request.pool, sizeof(pol_task_ctx));
  auto *task_ctx = new (task->ctx) pol_task_ctx{
      .req_ = request, .f_ = [this, &request, &span](ngx_log_t *log) {
        return this->run_waf_start(request, span);
      }};

  task->handler = &pol_task_ctx::handler;
  task->event.handler = &pol_task_ctx::completion_handler;
  task->event.data = task_ctx;

  if (ngx_thread_task_post(conf->waf_pool, task) != NGX_OK) {
    // log error
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "failed to post waf task");
    task_ctx->~pol_task_ctx();
    return false;
  } else {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "posted waf task");
    return true;
  }
}

namespace {
template <typename Map>
int getOrDefaultInt(const Map &m, std::string_view k, int def) {
  auto it = m.find(k);
  if (it == m.end()) {
    return def;
  }
  const action_info::str_or_int &v{it->second};
  if (std::holds_alternative<int>(v)) {
    return std::get<int>(v);
  } else {
    // try to convert to number
    std::string_view sv{std::get<std::string>(v)};
    int n;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), n);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
      return n;
    }
    return def;
  }
}

template <typename Map>
std::string_view getOrDefaultString(const Map &m, std::string_view k,
                                    std::string_view def) {
  auto it = m.find(k);
  if (it == m.end()) {
    return def;
  }
  const action_info::str_or_int &v{it->second};
  if (std::holds_alternative<std::string>(v)) {
    return {std::get<std::string>(v)};
  } else {
    return def;
  }
}

block_spec create_block_request_action(const action_info &ai) {
  static constexpr int default_status = 403;
  enum block_spec::ct ct{block_spec::ct::AUTO};
  int status = getOrDefaultInt(ai.parameters, "status_code"sv, default_status);
  if (status < 100 || status > 599) {
    status = default_status;
  }
  std::string_view ct_sv =
      getOrDefaultString(ai.parameters, "type"sv, "auto"sv);
  if (ct_sv == "auto"sv) {
    ct = block_spec::ct::AUTO;
  } else if (ct_sv == "html"sv) {
    ct = block_spec::ct::HTML;
  } else if (ct_sv == "json"sv) {
    ct = block_spec::ct::JSON;
  } else if (ct_sv == "none"sv) {
    ct = block_spec::ct::NONE;
  }

  return block_spec{status, ct};
}

std::optional<block_spec> create_redirect_request_action(
    const action_info &ai) {
  static constexpr int default_status = 303;
  enum block_spec::ct ct{block_spec::ct::NONE};
  int status = getOrDefaultInt(ai.parameters, "status_code"sv, default_status);
  if (status < 300 || status > 399) {
    status = default_status;
  }

  std::string_view loc = getOrDefaultString(ai.parameters, "location"sv, ""sv);
  if (loc == ""sv) {
    return std::nullopt;
  }

  return {block_spec{status, ct, loc}};
}

std::optional<block_spec> resolve_block_spec(
    const waf_handle::action_info_map_t &aim, const ddwaf_arr_obj &actions_arr,
    ngx_log_t &log) {
  for (auto &&id_obj : actions_arr) {
    if (id_obj.type != DDWAF_OBJ_STRING) {
      continue;
    }

    std::string_view id{id_obj.string_val_unchecked()};
    auto &&it = aim.find(id);
    if (it == aim.end()) {
      ngx_log_error(NGX_LOG_WARN, &log, 0,
                    "WAF indicated actions %.*s, but such action id is unknown",
                    static_cast<int>(id.size()), id.data());
      continue;
    }

    const action_info &ai = it->second;
    if (ai.type == "block_request"sv) {
      return {create_block_request_action(ai)};
    } else if (ai.type == "redirect_request"sv) {
      auto maybe_spec = create_redirect_request_action(ai);
      if (maybe_spec) {
        return maybe_spec;
      } else {
        ngx_log_error(NGX_LOG_WARN, &log, 0,
                      "redirect_request action has no location");
      }
    } else {
      ngx_log_error(NGX_LOG_WARN, &log, 0, "Action type %.*s is unknown",
                    static_cast<int>(ai.type.size()), ai.type.data());
      continue;
    }
  }
  return std::nullopt;
}
}  // namespace

std::optional<block_spec> context::run_waf_start(ngx_http_request_t &req,
                                                 dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::start) {
    return std::nullopt;
  }

  dd::SpanData &span_data = get_span_data(span);

  span_data.numeric_tags.insert_or_assign("_dd.appsec.enabled", 1.0);

  ddwaf_object *data = collect_request_data(req, memres_);

  ddwaf_result result;
  // TODO: configurable timeout
  auto code = ddwaf_run(ctx_.resource, data, nullptr, &result, 1000000);
  if (code == DDWAF_MATCH) {
    results_.emplace_back(result);
  } else {
    ddwaf_result_free(&result);
  }

  stage_->store(stage::after_begin_waf, std::memory_order_release);

  ddwaf_arr_obj actions_arr{result.actions};
  if (code != DDWAF_MATCH || actions_arr.nbEntries == 0) {
    return std::nullopt;
  }

  const waf_handle::action_info_map_t &aim = waf_handle_->action_info_map();
  return resolve_block_spec(aim, actions_arr, *req.connection->log);
}

void context::on_request_end(const ngx_http_request_t &request,
                             dd::Span &span) noexcept {
  return catch_exceptions("on_request_end", request,
                          [&]() { context::do_on_request_end(request, span); });
}

void context::do_on_request_end(const ngx_http_request_t &request,
                                dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::after_begin_waf) {
    return;
  }
  if (results_.empty()) {
    return;
  }

  auto &span_data = get_span_data(span);
  report_match(request, span.trace_segment(), span_data, results_);
  results_.clear();
  stage_->store(stage::after_report, std::memory_order_release);
}

}  // namespace security
}  // namespace nginx
}  // namespace datadog
