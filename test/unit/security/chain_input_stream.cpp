#include <security/body_parse/chain_input_stream.h>

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "managed_chain.h"

namespace dnsec = datadog::nginx::security;

namespace {
// Reads byte by byte until eof. Stops after max_reads, in case eof is never
// reached.
std::string read_bytes(dnsec::NgxChainInputStream &input_stream,
                       std::size_t max_reads = 64) {
  std::string result;
  while (!input_stream.eof() && result.size() < max_reads) {
    result.push_back(static_cast<char>(input_stream.read()));
  }
  return result;
}

class ChainStream {
 public:
  ChainStream(std::vector<std::string_view> parts,
              std::optional<std::size_t> max_bytes = std::nullopt)
      : parts_{std::move(parts)},
        managed_chain_{parts_},
        input_stream{&static_cast<ngx_chain_t &>(managed_chain_),
                     max_bytes.value_or(managed_chain_.size())} {}

 private:
  std::vector<std::string_view> parts_;
  test::ManagedChain managed_chain_;

 public:
  dnsec::NgxChainInputStream input_stream;
};

std::string read_bytes(std::vector<std::string_view> parts) {
  ChainStream chain_stream{std::move(parts)};
  return read_bytes(chain_stream.input_stream);
}
}  // namespace

TEST_CASE("chain stream reads across buffers", "[chain_input_stream]") {
  CHECK(read_bytes({"ab", "cd"}) == "abcd");
}

TEST_CASE("chain stream skips an empty buffer in the middle",
          "[chain_input_stream]") {
  CHECK(read_bytes({"ab", "", "", "cd"}) == "abcd");
}

TEST_CASE("chain stream skips an empty first buffer", "[chain_input_stream]") {
  ChainStream chain_stream{{"", "ab"}};
  REQUIRE(!chain_stream.input_stream.eof());
  CHECK(chain_stream.input_stream.peek() == 'a');
  CHECK(read_bytes(chain_stream.input_stream) == "ab");
}

TEST_CASE("chain stream ends at an empty last buffer", "[chain_input_stream]") {
  // no limit, so the empty buffer is not hidden by it
  ChainStream chain_stream{{"ab", ""}, std::numeric_limits<std::size_t>::max()};
  CHECK(read_bytes(chain_stream.input_stream) == "ab");
  CHECK(chain_stream.input_stream.eof());
}

TEST_CASE("chain stream stops at the limit", "[chain_input_stream]") {
  ChainStream chain_stream{{"abc", "def"}, 4};
  CHECK(read_bytes(chain_stream.input_stream) == "abcd");
}

TEST_CASE("chain stream bulk read skips empty buffers",
          "[chain_input_stream]") {
  ChainStream chain_stream{{"", "ab", "", "cd", ""}};
  std::uint8_t buf[8]{};
  CHECK(chain_stream.input_stream.read(buf, sizeof buf) == 4);
  CHECK(std::string_view{reinterpret_cast<char *>(buf), 4} == "abcd");
  CHECK(chain_stream.input_stream.eof());
}

TEST_CASE("chain stream counts the bytes read", "[chain_input_stream]") {
  ChainStream chain_stream{{"ab", "", "cdef"}};
  CHECK(chain_stream.input_stream.bytes_read() == 0);
  chain_stream.input_stream.read();
  CHECK(chain_stream.input_stream.bytes_read() == 1);
  std::uint8_t buf[2];
  chain_stream.input_stream.read(buf, sizeof buf);
  CHECK(chain_stream.input_stream.bytes_read() == 3);
  read_bytes(chain_stream.input_stream);
  CHECK(chain_stream.input_stream.bytes_read() == 6);
}
