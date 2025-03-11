#include "common/variable.h"

#include "string_util.h"

namespace datadog::common {

ngx_http_complex_value_t *make_complex_value(ngx_conf_t *cf, ngx_str_t &expr) {
  auto *cv = (ngx_http_complex_value_t *)ngx_pcalloc(
      cf->pool, sizeof(ngx_http_complex_value_t));

  ngx_http_compile_complex_value_t ccv;
  ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

  ccv.cf = cf;
  ccv.value = &expr;
  ccv.complex_value = cv;
  ccv.zero = 0;
  ccv.conf_prefix = 0;

  if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
    return nullptr;
  }

  return cv;
}

ngx_http_complex_value_t *make_complex_value(ngx_conf_t *cf,
                                             std::string_view expr) {
  auto ngx_expr = nginx::to_ngx_str(cf->pool, expr);
  return make_complex_value(cf, ngx_expr);
}

std::optional<std::string> eval_complex_value(
    ngx_http_complex_value_t *complex_value, ngx_http_request_t *request) {
  if (complex_value == nullptr) return std::nullopt;

  ngx_str_t res;
  if (ngx_http_complex_value(request, complex_value, &res) != NGX_OK ||
      res.len == 0) {
    return std::nullopt;
  }

  return nginx::to_string(res);
}

}  // namespace datadog::common
