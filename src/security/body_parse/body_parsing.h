#pragma once

extern "C" {
#include <ngx_http.h>
}

#include <span>
#include <string_view>
#include <vector>

#include "../ddwaf_obj.h"
#include "../waf_input_budget.h"

namespace datadog::nginx::security {

bool parse_body_req(ddwaf_obj &slot, const ngx_http_request_t &req,
                    const ngx_chain_t &chain, std::size_t size,
                    WafInputBudget &budget, DdwafMemres &memres);

bool is_body_resp_parseable(const ngx_http_request_t &req);

bool parse_body_resp(ddwaf_obj &slot, const ngx_http_request_t &req,
                     const ngx_chain_t &chain, std::size_t size,
                     WafInputBudget &budget, DdwafMemres &memres);

struct KeyValues {
  std::string_view key;
  std::vector<std::string_view> values;
};

// Fills slot with a map from each key to its value, or to an array of its
// values if it has several. The strings are not copied, so they must outlive
// slot. Stops when the budget runs out; a key without room for its value is
// kept with an invalid value, as in the JSON parser. Returns false if not
// even the map fits.
bool make_grouped_map(ddwaf_obj &slot, std::span<const KeyValues> groups,
                      WafInputBudget &budget, DdwafMemres &memres);

}  // namespace datadog::nginx::security
