#pragma once

#include <datadog/dict_writer.h>

#include "common/headers.h"
#include "string_util.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

class NgxHeaderWriter : public datadog::tracing::DictWriter {
  ngx_http_request_t *request_;
  ngx_pool_t *pool_;

 public:
  explicit NgxHeaderWriter(ngx_http_request_t *request)
      : request_(request), pool_(request_->pool) {}

  void set(std::string_view key, std::string_view value) override {
    ngx_table_elt_t *h =
        common::search_header(request_->headers_in.headers, key);
    if (h != nullptr) {
      h->value = to_ngx_str(pool_, value);
    } else {
      common::add_header(*pool_, request_->headers_in.headers, key, value);
    }
  }

  void erase(std::string_view key) override {
    common::remove_header(request_->headers_in.headers, key);
  }
};

}  // namespace nginx
}  // namespace datadog
