#include "ngx_http_client.h"

#include <datadog/dict_writer.h>

#include <datadog/json.hpp>

#include "common/http_client/client.h"

namespace datadog {
namespace tracing {

class GenericWriter : public DictWriter {
  using WriterCallback = std::function<void(StringView, StringView)>;

  WriterCallback on_write_;

 public:
  GenericWriter(WriterCallback on_write) : on_write_(std::move(on_write)) {}
  ~GenericWriter() override = default;
};

Expected<void> NgxHTTPClient::post(
    const URL& url, HeadersSetter set_headers, std::string body,
    ResponseHandler on_response, ErrorHandler on_error,
    std::chrono::steady_clock::time_point deadline) {
  /*common::http::message payload;*/
  /*payload.set_method(common::http::method::POST);*/
  /*payload.set_body(std::move(body));*/
  /**/
  /*GenericWriter writer([&payload](StringView key, StringView value) {*/
  /*  payload.set_header(key, value);*/
  /*});*/
  /**/
  /*set_headers(writer);*/
  /**/
  /*common::http::client::options opts;*/
  /*opts.timeout(1000);*/

  // TODO: resolver from http_core
  // clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
  // clcf->resolver
  /*auto status = common::http::client::make_request(url, message, );*/
  /*if (!status) {*/
  /*  return Error{};*/
  /*}*/
  /*common::http::resolve(resolver_, url.authority);*/
  /*std::string full_url{url.scheme};*/
  /*full_url += "://";*/
  /*full_url += url.authority;*/
  /*common::http::send(pool_, std::string_view(full_url));*/
  std::string host("localhost:8126");
  common::http::send(pool_, log_, host);

  return {};
}

void NgxHTTPClient::drain(std::chrono::steady_clock::time_point deadline){};

nlohmann::json NgxHTTPClient::config_json() const { return {}; };

}  // namespace tracing
}  // namespace datadog
