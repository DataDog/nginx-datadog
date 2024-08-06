#define CPPHTTPLIB_OPENSSL_SUPPORT

#include <csignal>
#include <datadog/json.hpp>

#include "datadog_conf.h"
#include "httplib.h"
#include "string_util.h"

namespace datadog::rum {

inline std::string search_default_application(const nlohmann::json& j) {
  for (const auto& app_info : j["data"]) {
    const auto& app_attr = app_info["attributes"];
    if (app_attr["name"] == "default_nginx") {
      return app_attr["application_id"];
    }
  }

  return "";
}

inline std::string create_default_application(httplib::Client& client) {
  constexpr char body[] = R"EOF({
    "data": {
      "attributes": {
        "name": "default_nginx",
        "type": "browser"
      },
      "type": "rum_application_create"
    }
  })EOF";

  auto res = client.Post("/api/v2/rum/applications", body, "application/json");
  if (res->status != 200) {
    return "";
  }

  auto j = nlohmann::json::parse(res->body);
  return j["data"]["attributes"]["application_id"];
}

inline std::string init(nginx::datadog_main_conf_t& conf) {
  /*raise(SIGSTOP);*/
  httplib::Client client("https://api.datadoghq.eu");
  client.set_default_headers({
      {"Accept", "application/json"},
      {"DD-API-KEY", nginx::to_string(conf.api_key)},
      {"DD-APPLICATION-KEY", nginx::to_string(conf.app_key)},
  });

  auto res = client.Get("/api/v2/rum/applications");
  if (res->status != 200) {
    return "";
  }

  auto j = nlohmann::json::parse(res->body);

  auto default_app_id = search_default_application(j);
  if (default_app_id.empty()) {
    default_app_id = create_default_application(client);
    if (default_app_id.empty()) {
      return "";
    }
  }

  return default_app_id;
}

}  // namespace datadog::rum
