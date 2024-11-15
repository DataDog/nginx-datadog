#include <fuzzer/FuzzedDataProvider.h>
#include <security/body_parse/body_multipart.h>
#include <security/body_parse/header.h>
#include <security/ddwaf_memres.h>
#include <security/ddwaf_obj.h>

#include "../unit/managed_chain.h"

extern "C" {
#include <ngx_core.h>
#include <ngx_http.h>
}

extern "C" {
#include "../unit/stub_nginx.c"
}

namespace dnsec = datadog::nginx::security;

// Mock structures to satisfy dependencies
static ngx_log_t log{};
static ngx_connection_t empty_conn{.log = &log};

// Fuzzer entry point
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // Wrap data with FuzzedDataProvider to extract parts safely
  FuzzedDataProvider fdp{data, size};

  if (fdp.remaining_bytes() == 0) {
    return 0;
  }

  // Generate a vector of random multipart segments
  std::vector<std::vector<char>> parts;
  while (fdp.remaining_bytes()) {
    parts.emplace_back(fdp.ConsumeBytes<char>(21));
  }

  // Set up memory resources for parsing
  dnsec::DdwafMemres memres;

  // Prepare the ngx_http_request_t structure
  dnsec::ddwaf_obj slot;
  ngx_http_request_t req{.connection = &empty_conn};

  // Attempt to parse the multipart data
  try {
    std::vector<std::string_view> parts_sv;
    for (const auto &part : parts) {
      parts_sv.emplace_back(part.data(), part.size());
    }
    test::ManagedChain chain{parts_sv};
    auto ct = dnsec::HttpContentType::for_string(
        "multipart/form-data; boundary=myboundary");

    if (!ct) {
      return 0;
    }

    parse_multipart(slot, req, *ct, chain, memres);
  } catch (const std::exception &) {
    // Ignore exceptions to allow fuzzing to continue
  }

  return 0;
}
