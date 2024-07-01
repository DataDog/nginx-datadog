#include <datadog/http_client.h>

extern "C" {
#include <ngx_core.h>
}

namespace datadog {
namespace tracing {

class NgxHTTPClient : public HTTPClient {
  ngx_log_t* log_;
  ngx_pool_t* pool_;
  ngx_resolver_t* resolver_;

 public:
  NgxHTTPClient(ngx_log_t* log, ngx_pool_t* pool, ngx_resolver_t* resolver)
      : log_(log), pool_(pool), resolver_(resolver) {}
  ~NgxHTTPClient() override = default;

  Expected<void> post(const URL& url, HeadersSetter set_headers,
                      std::string body, ResponseHandler on_response,
                      ErrorHandler on_error,
                      std::chrono::steady_clock::time_point deadline) override;

  // Wait until there are no more outstanding requests, or until the specified
  // `deadline`.
  void drain(std::chrono::steady_clock::time_point deadline) override;

  // Return a JSON representation of this object's configuration. The JSON
  // representation is an object with the following properties:
  //
  // - "type" is the unmangled, qualified name of the most-derived class, e.g.
  //   "datadog::tracing::Curl".
  // - "config" is an object containing this object's configuration. "config"
  //   may be omitted if the derived class has no configuration.
  nlohmann::json config_json() const override;
};

}  // namespace tracing
}  // namespace datadog
