#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <vector>

extern "C" {
#include <arpa/inet.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

#include "security/client_ip.h"

namespace dnsec = datadog::nginx::security;

namespace {
sockaddr_in create_ipv4_sockaddr(const char* ip_str) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, ip_str, &addr.sin_addr);
  return addr;
}

sockaddr_in6 create_ipv6_sockaddr(const char* ip_str) {
  sockaddr_in6 addr{};
  addr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, ip_str, &addr.sin6_addr);
  return addr;
}

struct Address {
  sockaddr* sockaddr_ptr;
  sockaddr_storage sockaddr_storage;

  Address(const char* ip_str, bool is_ipv6 = false) {
    if (is_ipv6) {
      auto addr = create_ipv6_sockaddr(ip_str);
      std::memcpy(&sockaddr_storage, &addr, sizeof(addr));
    } else {
      auto addr = create_ipv4_sockaddr(ip_str);
      std::memcpy(&sockaddr_storage, &addr, sizeof(addr));
    }
    sockaddr_ptr = reinterpret_cast<sockaddr*>(&sockaddr_storage);
  }
};

struct StubRequest {
  ngx_http_request_t request;
  ngx_connection_t connection;
  Address address;
  ngx_pool_t pool;
  std::vector<ngx_table_elt_t> headers;
  std::vector<ngx_str_t> header_keys;
  std::vector<ngx_str_t> header_values;

  StubRequest(const char* remote_ip, bool is_ipv6 = false)
      : request{}, connection{}, address{remote_ip, is_ipv6}, pool{} {
    connection.sockaddr = address.sockaddr_ptr;
    request.connection = &connection;
    request.pool = &pool;

    request.headers_in.headers.part.elts = nullptr;
    request.headers_in.headers.part.nelts = 0;
    request.headers_in.headers.part.next = nullptr;
    request.headers_in.headers.last = &request.headers_in.headers.part;
    request.headers_in.headers.pool = &pool;
  }

  void add_header(const char* key, const char* value) {
    // Store the key and value strings
    ngx_str_t key_str;
    key_str.data = reinterpret_cast<u_char*>(const_cast<char*>(key));
    key_str.len = std::strlen(key);
    header_keys.push_back(key_str);

    ngx_str_t value_str;
    value_str.data = reinterpret_cast<u_char*>(const_cast<char*>(value));
    value_str.len = std::strlen(value);
    header_values.push_back(value_str);

    ngx_table_elt_t header{};
    header.key = header_keys.back();
    header.value = header_values.back();
    header.lowcase_key = header.key.data;  // same pointer for simplicity
    header.hash = ngx_hash_key(header.lowcase_key, header.key.len);
    headers.push_back(header);

    request.headers_in.headers.part.elts = headers.data();
    request.headers_in.headers.part.nelts = headers.size();
  }
};
}  // namespace

TEST_CASE("ClientIp priority: public IP in header and public remote_addr",
          "[client_ip]") {
  StubRequest stub("8.8.8.8");
  stub.add_header("x-forwarded-for", "1.1.1.1");

  dnsec::ClientIp client_ip(std::nullopt, stub.request);
  auto result = client_ip.resolve();

  REQUIRE(result.has_value());
  REQUIRE(result.value() == "1.1.1.1");  // Header is preferred
}

TEST_CASE("ClientIp priority: private IP in header and public remote_addr",
          "[client_ip]") {
  StubRequest stub("8.8.8.8");
  stub.add_header("x-forwarded-for", "192.168.1.1");

  dnsec::ClientIp client_ip(std::nullopt, stub.request);
  auto result = client_ip.resolve();

  REQUIRE(result.has_value());
  REQUIRE(result.value() == "8.8.8.8");  // Public remote_addr is used
}

TEST_CASE("ClientIp priority: private IP in header and private remote_addr",
          "[client_ip]") {
  StubRequest stub("192.168.1.100");
  stub.add_header("x-forwarded-for", "10.0.0.5");

  dnsec::ClientIp client_ip(std::nullopt, stub.request);
  auto result = client_ip.resolve();

  REQUIRE(result.has_value());
  REQUIRE(result.value() == "10.0.0.5");  // Header is preferred
}

TEST_CASE("ClientIp fallback: only private remote_addr", "[client_ip]") {
  StubRequest stub("192.168.1.100");

  dnsec::ClientIp client_ip(std::nullopt, stub.request);
  auto result = client_ip.resolve();

  REQUIRE(result.has_value());
  REQUIRE(result.value() == "192.168.1.100");  // Remote_addr is used
}

TEST_CASE("ClientIp fallback: only private IP in header", "[client_ip]") {
  StubRequest stub("0.0.0.0");  // Invalid/empty remote_addr
  // Set af to 0 to simulate no remote address
  reinterpret_cast<sockaddr_in*>(stub.address.sockaddr_ptr)->sin_family = 0;
  stub.add_header("x-forwarded-for", "10.0.0.5");

  dnsec::ClientIp client_ip(std::nullopt, stub.request);
  auto result = client_ip.resolve();

  REQUIRE(result.has_value());
  REQUIRE(result.value() == "10.0.0.5");  // Header is used
}
