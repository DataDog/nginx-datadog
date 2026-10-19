#include "security/collection.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <vector>

#include "security/ddwaf_obj.h"

extern "C" {
#include <ngx_http.h>
}

namespace dnsec = datadog::nginx::security;

using namespace std::literals::string_view_literals;

namespace {
// A response with the given headers. nginx deletes a header by setting its
// hash to 0.
struct StubResponse {
  ngx_http_request_t request{};
  std::vector<ngx_table_elt_t> headers;

  void add_header(std::string_view key, std::string_view value,
                  bool deleted = false) {
    ngx_table_elt_t header{};
    header.key = {key.size(),
                  reinterpret_cast<u_char *>(const_cast<char *>(key.data()))};
    header.value = {value.size(), reinterpret_cast<u_char *>(
                                      const_cast<char *>(value.data()))};
    header.hash = deleted ? 0 : 1;
    headers.push_back(header);

    ngx_list_t &list = request.headers_out.headers;
    list.part.elts = headers.data();
    list.part.nelts = headers.size();
    list.part.next = nullptr;
    list.last = &list.part;
  }
};
}  // namespace

TEST_CASE("response headers: the first copy of a repeated header is deleted",
          "[collection]") {
  StubResponse stub;
  stub.add_header("X-Dup", "a", true);
  stub.add_header("X-Dup", "b");
  stub.add_header("X-Dup", "c");

  dnsec::DdwafMemres memres;
  ddwaf_object *data =
      dnsec::collect_response_data(stub.request, nullptr, 0, false, memres);

  dnsec::ddwaf_map_obj root{*data};
  auto headers = root.get_opt<dnsec::ddwaf_map_obj>(
      "server.response.headers.no_cookies"sv);
  REQUIRE(headers);
  auto values = headers->get_opt<dnsec::ddwaf_arr_obj>("x-dup"sv);
  REQUIRE(values);
  REQUIRE(values->size() == 2);
  CHECK(values->at<dnsec::ddwaf_str_obj>(0).value() == "b");
  CHECK(values->at<dnsec::ddwaf_str_obj>(1).value() == "c");
}
