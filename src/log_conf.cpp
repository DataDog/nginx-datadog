#include "log_conf.h"

#include "datadog_conf.h"
#include "datadog_conf_handler.h"
#include "defer.h"
#include "ngx_http_datadog_module.h"
#include "string_util.h"
#include <string_view>

namespace datadog {
namespace nginx {

ngx_int_t inject_datadog_log_formats(ngx_conf_t *conf) {
  // This retrieval of `main_conf` is undefined behavior unless we're already
  // inside the `http` configuration block.
  // One way to ensure this is to only call `inject_datadog_log_formats`
  // in handlers of directives that only appear within an `http` block,
  // such as `server` and `access_log`.
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(conf, ngx_http_datadog_module));

  assert(main_conf != nullptr);

  // If the log formats are already defined, don't bother.
  if (main_conf->are_log_formats_defined) {
    return NGX_OK;
  }

  // Set up `log_format ...` commands, and then use `datadog_conf_handler` to
  // execute them.

  // log_format <name> <escaping style> <format>
  ngx_str_t args[] = {ngx_string("log_format"), ngx_str_t(), ngx_str_t(), ngx_str_t()};
  ngx_array_t args_array;
  args_array.elts = args;
  args_array.nelts = sizeof args / sizeof args[0];

  auto old_args = conf->args;
  conf->args = &args_array;
  const auto guard = defer([&]() { conf->args = old_args; });

  struct {
    const char *name;
    const char *escaping_style;
    const char *format;
  } static const formats[] = {
      {"datadog_text", "escape=default",
       R"nginx($remote_addr - $http_x_forwarded_user [$time_local] "$request" $status $body_bytes_sent "$http_referer" "$http_user_agent" "$http_x_forwarded_for" "$datadog_trace_id" "$datadog_span_id")nginx"},
      {"datadog_json", "escape=json",
       R"json({"remote_addr": "$remote_addr", "forwarded_user": "$http_x_forwarded_user", "time_local": "$time_local", "request": "$request", "status": $status, "body_bytes_sent": $body_bytes_sent, "referer": "$http_referer", "user_agent": "$http_user_agent", "forwarded_for": "$http_x_forwarded_for", "trace_id": "$datadog_trace_id", "span_id": "$datadog_span_id"})json"}};

  for (const auto &format : formats) {
    args[1] = to_ngx_str(std::string_view(format.name));
    args[2] = to_ngx_str(std::string_view(format.escaping_style));
    args[3] = to_ngx_str(std::string_view(format.format));
    if (auto rcode = datadog_conf_handler({.conf = conf, .skip_this_module = true})) {
      return rcode;
    }
  }

  main_conf->are_log_formats_defined = true;
  return NGX_OK;
}

}  // namespace nginx
}  // namespace datadog
