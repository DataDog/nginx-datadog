#include <fstream>
#include "datadog_conf.h"
#include "ngx_http_datadog_module.h"
#include "security_collection.h"
extern "C" {
#include <ngx_hash.h>
#include <ngx_http.h>
#include <ngx_regex.h>
#include <ngx_string.h>
#include <ngx_thread_pool.h>
}
#include <sstream>
#include <string>

#include <rapidjson/prettywriter.h>
#include <type_traits>
#include <unordered_set>

#include <ddwaf.h>
#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include "datadog/span_data.h"
#include "datadog/trace_segment.h"
#include "security_context.h"
#include "tracing_library.h"

using namespace rapidjson;

namespace {

class JsonWriter : public rapidjson::Writer<rapidjson::StringBuffer> {
  using rapidjson::Writer<rapidjson::StringBuffer>::Writer;

 public:
  template <size_t N>
  bool LiteralKey(const char (&name)[N]) {
    return String(name, N - 1, false);
  }
};

void ddwaf_object_to_json(JsonWriter &w, const ddwaf_object &dobj);

void report_match(const ngx_http_request_t &req, dd::TraceSegment &seg,
                  dd::SpanData &span,
                  std::vector<datadog::nginx::owned_ddwaf_result> &results) {
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
  w.LiteralKey("triggers");

  w.StartArray();
  for (auto &&result : results) {
    auto &&events = (*result).events;
    for (size_t i = 0; i < events.nbEntries; i++) {
      auto &&evt = events.array[i];
      ddwaf_object_to_json(w, evt);
    }
  }
  w.EndArray(results.size());

  w.EndObject(1);
  w.Flush();

  const char *json_str = buffer.GetString();
  size_t json_str_len = buffer.GetLength();

  ngx_log_error(NGX_LOG_WARN, req.connection->log, 0, "appsec event: %.*s",
                static_cast<int>(json_str_len), json_str);

  span.tags[std::string{APPSEC_JSON}] = std::string{json_str, json_str_len};
}

void ddwaf_object_to_json(JsonWriter &w, const ddwaf_object &dobj) {
  switch (dobj.type) {
    case DDWAF_OBJ_MAP:
      w.StartObject();
      for (size_t i = 0; i < dobj.nbEntries; i++) {
        auto &&e = dobj.array[i];
        w.Key(e.parameterName, e.parameterNameLength, false);
        ddwaf_object_to_json(w, e);
      }
      w.EndObject(dobj.nbEntries);
      break;
    case DDWAF_OBJ_ARRAY:
      w.StartArray();
      for (size_t i = 0; i < dobj.nbEntries; i++) {
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

template <typename Callable, typename Ret = decltype(std::declval<Callable>()())>
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

decltype(auto) get_span_data(dd::Span &s) {
  auto *p = reinterpret_cast<char *>(&s) + sizeof(std::shared_ptr<dd::TraceSegment>);
  dd::SpanData *span_data;
  std::memcpy(&span_data, p, sizeof(span_data)); // NOLINT
  if (span_data == nullptr) {
    throw std::runtime_error(
        "get_span_data failed: could not find span data");
  }
  return *span_data;
}
} // namespace

namespace datadog {
namespace nginx {

security_context::security_context() : stage_{new std::atomic<stage>{}} {
  std::shared_ptr<waf_handle> handle = security_library::get_handle();
  if (!handle) {
    return;
    }

    ctx_ = ddwaf_context_init(handle->get());
    stage_->store(stage::start, std::memory_order_release);
    if (ctx_.resource == nullptr) {
        return;
    }
}


bool security_context::on_request_start(ngx_http_request_t &request,
                                      dd::Span &span) noexcept {
  return catch_exceptions(
      "on_request_start", request, [&]() {
        return security_context::do_on_request_start(request, span);
      });
}

struct pol_task_ctx {
  static void handler(void *self, ngx_log_t *log) noexcept {
      static_cast<pol_task_ctx *>(self)->handle(log);
  }

  static void completion_handler(ngx_event_t *evt) {
      pol_task_ctx *ctx = static_cast<pol_task_ctx*>(evt->data);

      ctx->~pol_task_ctx();

      ngx_log_debug(NGX_LOG_DEBUG_HTTP, ctx->req_.connection->log, 0,
                    "completion, finalize %p", &ctx->req_);

      ctx->req_.phase_handler++; // move past us
      ngx_http_core_run_phases(&ctx->req_);
  }

  void handle(ngx_log_t *log) {
    try {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "before task main: %p", &req_);;
      f_(log);
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "after task main: %p", &req_);;
    } catch (std::exception &e) {
      ngx_log_error(NGX_LOG_ERR, log, 0, "task failed: %s",
                    e.what());
    } catch (...) {
      ngx_log_error(NGX_LOG_ERR, log, 0, "task failed: unknown failure");
    }
  }

  ngx_http_request_t &req_;
  std::function<void(ngx_log_t *)> f_;
};

bool security_context::do_on_request_start(
    ngx_http_request_t &request, dd::Span &span) {
  if (ctx_.resource == nullptr) {
    return false;
  }

  auto *conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));

  if (conf->waf_pool == nullptr) {
    return false;
  }

  ngx_thread_task_t *task = ngx_thread_task_alloc(request.pool,
    sizeof(pol_task_ctx));
  auto *task_ctx = new (task->ctx) pol_task_ctx{
    .req_ = request,
    .f_ = [this, &request, &span](ngx_log_t *log) {
      this->run_waf_start(request, span);
    }
  };

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

void security_context::run_waf_start(const ngx_http_request_t &request,
                                     dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::start) {
    return;
  }

  dd::SpanData &span_data = get_span_data(span);

  span_data.numeric_tags.insert_or_assign("_dd.appsec.enabled", 1.0);

  ddwaf_object *data = collect_request_data(request, memres_);

  ddwaf_result result;
  // TODO: configurable timeout
  auto code = ddwaf_run(ctx_.resource, data, nullptr, &result, 1000000);
  if (code == DDWAF_MATCH) {
    results_.emplace_back(result);
  } else {
    ddwaf_result_free(&result);
  }

  stage_->store(stage::after_begin_waf, std::memory_order_release);
}

void security_context::on_request_end(const ngx_http_request_t &request,
                                    dd::Span &span) noexcept {
  return catch_exceptions(
      "on_request_end", request, [&]() {
        security_context::do_on_request_end(request, span);
      });
}

void security_context::do_on_request_end(const ngx_http_request_t &request,
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

}  // namespace nginx
}  // namespace datadog
