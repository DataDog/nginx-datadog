#include <security/body_parse/chain_is.h>

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "managed_chain.h"

namespace dnsec = datadog::nginx::security;

namespace {
// Reads byte by byte until eof. Stops after max_reads, in case eof is never
// reached.
std::string read_bytes(dnsec::NgxChainInputStream &is,
                       std::size_t max_reads = 64) {
  std::string res;
  while (!is.eof() && res.size() < max_reads) {
    res.push_back(static_cast<char>(is.read()));
  }
  return res;
}

std::string read_bytes(std::vector<std::string_view> parts) {
  test::ManagedChain chain{parts};
  dnsec::NgxChainInputStream is{&static_cast<ngx_chain_t &>(chain),
                                chain.size()};
  return read_bytes(is);
}
}  // namespace

TEST_CASE("chain stream reads across buffers", "[chain_is]") {
  CHECK(read_bytes({"ab", "cd"}) == "abcd");
}

TEST_CASE("chain stream skips an empty buffer in the middle", "[chain_is]") {
  CHECK(read_bytes({"ab", "", "", "cd"}) == "abcd");
}

TEST_CASE("chain stream skips an empty first buffer", "[chain_is]") {
  std::vector<std::string_view> parts{"", "ab"};
  test::ManagedChain chain{parts};
  dnsec::NgxChainInputStream is{&static_cast<ngx_chain_t &>(chain),
                                chain.size()};
  REQUIRE(!is.eof());
  CHECK(*is == 'a');
  CHECK(read_bytes(is) == "ab");
}

TEST_CASE("chain stream ends at an empty last buffer", "[chain_is]") {
  std::vector<std::string_view> parts{"ab", ""};
  test::ManagedChain chain{parts};
  // no limit, so the empty buffer is not hidden by it
  dnsec::NgxChainInputStream is{&static_cast<ngx_chain_t &>(chain), SIZE_MAX};
  CHECK(read_bytes(is) == "ab");
  CHECK(is.eof());
}

TEST_CASE("chain stream stops at the limit", "[chain_is]") {
  std::vector<std::string_view> parts{"abc", "def"};
  test::ManagedChain chain{parts};
  dnsec::NgxChainInputStream is{&static_cast<ngx_chain_t &>(chain), 4};
  CHECK(read_bytes(is) == "abcd");
}

TEST_CASE("chain stream bulk read skips empty buffers", "[chain_is]") {
  std::vector<std::string_view> parts{"", "ab", "", "cd", ""};
  test::ManagedChain chain{parts};
  dnsec::NgxChainInputStream is{&static_cast<ngx_chain_t &>(chain),
                                chain.size()};
  std::uint8_t buf[8]{};
  CHECK(is.read(buf, sizeof buf) == 4);
  CHECK(std::string_view{reinterpret_cast<char *>(buf), 4} == "abcd");
  CHECK(is.eof());
}
