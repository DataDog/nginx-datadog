#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <rapidjson/prettywriter.h>
#include <rapidjson/schema.h>
#include <rapidjson/error/en.h>

#include "security_context.h"

using namespace rapidjson;

namespace datadog {
namespace nginx {

security_context::security_context() {
    auto handle = security_library::get_handle();
    if (!handle) {
        return;
    }

    ctx_ = ddwaf_context_init(handle->get());
    if (ctx_ == nullptr) {
        return;
    }
}

security_context::~security_context() {
    if (ctx_ != nullptr) {
        ddwaf_context_destroy(ctx_);
    }
}

namespace {

void set_request_query(ngx_http_request_t *request, ddwaf_object *root)
{
    if (request->args.len == 0 || request->args.data == nullptr) { return; }

    // FIXME: args contains more than the query string
    ddwaf_object tmp;
    ddwaf_object_map_add(root, "server.request.query",
        ddwaf_object_stringl(&tmp, (char *)request->args.data, request->args.len));
}

void set_request_uri_raw(ngx_http_request_t *request, ddwaf_object *root)
{
    if (request->uri.len == 0 || request->uri.data == nullptr) { return; }

    ddwaf_object tmp;
    ddwaf_object_map_add(root, "server.request.uri.raw",
        ddwaf_object_stringl(&tmp, (char *)request->uri.data, request->uri.len));
}

void set_request_headers_nocookies(ngx_http_request_t *request, ddwaf_object *root)
{
    ddwaf_object headers, tmp;
    ddwaf_object_map(&headers);

    auto &headers_in = request->headers_in;

    // User agent
    {
        auto *user_agent = headers_in.user_agent;
        if (user_agent != nullptr &&
                user_agent->value.data != nullptr && user_agent->value.len > 0) {
            ddwaf_object_map_add(&headers, "user-agent",
                    ddwaf_object_string(&tmp, (char *)user_agent->value.data));
        }
    }

    // Referer
    {
        auto *referer = headers_in.referer;
        if (referer != nullptr &&
                referer->value.data != nullptr && referer->value.len > 0) {
            ddwaf_object_map_add(&headers, "referer",
                    ddwaf_object_string(&tmp, (char *)referer->value.data));
        }
    }

    ddwaf_object_map_add(root, "server.request.headers.no_cookies", &headers);
}

void set_request_method(ngx_http_request_t *request, ddwaf_object *root)
{
    if (request->method_name.len == 0 || request->method_name.data == nullptr) {
        return;
    }

    ddwaf_object tmp;
    ddwaf_object_map_add(root, "server.request.method",
        ddwaf_object_stringl(&tmp, (char *)request->method_name.data, request->method_name.len));
}

void set_request_cookie(ngx_http_request_t *request, ddwaf_object *root)
{
    auto *cookie = request->headers_in.cookie;
    if (cookie == nullptr || cookie->value.data == nullptr || cookie->value.len == 0) {
        return;
    }

    ddwaf_object tmp;
    ddwaf_object_map_add(root, "server.request.cookies",
        ddwaf_object_stringl(&tmp, (char *)cookie->value.data, cookie->value.len));
}
}

void security_context::on_log_request(ngx_http_request_t* request, ot::Span &span)
{
    if (ctx_ == nullptr) { return; }

    // FIXME: This should be a metric
    span.SetTag("_dd.appsec.enabled", 1);

    ddwaf_object data;
    ddwaf_object_map(&data);
    set_request_uri_raw(request, &data);
    set_request_query(request, &data);
    set_request_method(request, &data);
    set_request_headers_nocookies(request, &data);
    set_request_cookie(request, &data);

    ddwaf_result result;
    auto code = ddwaf_run(ctx_, &data, &result, 1000000);
    if (code == DDWAF_MATCH && result.data != nullptr) {
        span.SetTag("appsec.event", true);

        std::stringstream ss;
        ss << R"({"triggers":)" << result.data << "}";
        span.SetTag("_dd.appsec.json", ss.str());
    }

    ddwaf_result_free(&result);
}

}
}
