#include "body_parsing.h"

#include <ddwaf.h>
#include <ngx_string.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../ddwaf_memres.h"
#include "../ddwaf_obj.h"
#include "../decode.h"
#include "body_json.h"
#include "body_multipart.h"
#include "chain_is.h"
#include "header.h"

extern "C" {
#include <ngx_core.h>
}

using namespace std::literals;

namespace dnsec = datadog::nginx::security;

namespace {

bool is_content_type(std::string_view actual, std::string_view tested) {
  auto sv = actual;
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
    sv.remove_prefix(1);
  }

  if (sv.size() < tested.size()) {
    return false;
  }

  for (std::size_t i = 0; i < tested.size(); i++) {
    if (std::tolower(sv.at(i)) != tested.at(i)) {
      return false;
    }
  }
  sv.remove_prefix(tested.size());

  return sv.empty() || sv.front() == ';' || sv.front() == ' ' ||
         sv.front() == '\t';
}

bool is_req_json(const ngx_http_request_t &req) {
  const ngx_table_elt_t *ct = req.headers_in.content_type;
  // don't look at ct->next; consider only the first value
  return ct && is_content_type(datadog::nginx::to_string_view(ct->value),
                               "application/json"sv);
}

bool is_resp_json(const ngx_http_request_t &req) {
  const ngx_str_t ct = req.headers_out.content_type;
  return is_content_type(datadog::nginx::to_string_view(ct),
                         "application/json"sv);
}

bool is_req_multipart(const ngx_http_request_t &req) {
  const ngx_table_elt_t *ct = req.headers_in.content_type;
  return ct && is_content_type(datadog::nginx::to_string_view(ct->value),
                               "multipart/form-data"sv);
}

bool is_req_urlencoded(const ngx_http_request_t &req) {
  const ngx_table_elt_t *ct = req.headers_in.content_type;
  return ct && is_content_type(datadog::nginx::to_string_view(ct->value),
                               "application/x-www-form-urlencoded"sv);
}
bool is_req_text_plain(const ngx_http_request_t &req) {
  const ngx_table_elt_t *ct = req.headers_in.content_type;
  return ct && is_content_type(datadog::nginx::to_string_view(ct->value),
                               "text/plain"sv);
}

bool is_resp_text_plain(const ngx_http_request_t &req) {
  const ngx_str_t ct = req.headers_out.content_type;
  return is_content_type(datadog::nginx::to_string_view(ct), "text/plain"sv);
}

char *linearize_chain(const ngx_chain_t &chain, std::size_t size,
                      dnsec::DdwafMemres &memres) {
  char *buf = memres.allocate_string(size);
  dnsec::NgxChainInputStream stream{&chain, size};
  auto read = stream.read(reinterpret_cast<std::uint8_t *>(buf), size);
  if (read < size) {
    throw std::runtime_error(
        "mismatch between declared size and read size (read is smaller than "
        "declared)");
  }
  return buf;
}

bool parse_plain(dnsec::ddwaf_obj &slot, const ngx_chain_t &chain,
                 std::size_t size, dnsec::WafInputBudget &budget,
                 dnsec::DdwafMemres &memres) {
  char *buf = linearize_chain(chain, size, memres);

  std::optional<std::string_view> kept =
      budget.charge_string({buf, size}, false);
  if (!kept) {
    return false;
  }
  slot.make_string(*kept);
  return true;
}

bool parse_urlencoded(dnsec::ddwaf_obj &slot, const ngx_chain_t &chain,
                      std::size_t size, dnsec::WafInputBudget &budget,
                      dnsec::DdwafMemres &memres) {
  char *buf = linearize_chain(chain, size, memres);

  dnsec::QueryStringIter it{
      {buf, size}, memres, '&', dnsec::QueryStringIter::trim_mode::no_trim};

  // group the values by key, in the order the keys first appear
  std::vector<dnsec::KeyValues> groups;
  std::unordered_map<std::string_view, std::size_t> key_index;
  for (; !it.ended(); ++it) {
    auto [key, value] = *it;
    auto [pos, inserted] = key_index.try_emplace(key, groups.size());
    if (inserted) {
      groups.push_back({key, {}});
    }
    groups[pos->second].values.push_back(value);
  }

  return dnsec::make_grouped_map(slot, groups, budget, memres);
}

}  // namespace

namespace datadog::nginx::security {

bool make_grouped_map(ddwaf_obj &slot, std::span<const KeyValues> groups,
                      WafInputBudget &budget, DdwafMemres &memres) {
  if (!budget.charge_container(false)) {
    return false;
  }

  ddwaf_map_obj &map = slot.make_map(groups.size(), memres);
  std::size_t kept = 0;
  for (const KeyValues &group : groups) {
    std::optional<std::string_view> key = budget.charge_string(group.key, true);
    if (!key) {
      break;
    }
    ddwaf_kv_obj &entry = map.at_unchecked(kept);
    entry.set_key(*key);

    if (group.values.size() == 1) {
      // if only one value, put the string directly under the key
      std::optional<std::string_view> value =
          budget.charge_string(group.values.front(), false);
      if (!value) {
        // keep the key, so rules on keys still see it
        entry.val.make_null();
        kept++;
        break;
      }
      entry.val.make_string(*value);
    } else {
      if (!budget.charge_container(false)) {
        entry.val.make_null();
        kept++;
        break;
      }
      ddwaf_arr_obj &arr = entry.val.make_array(group.values.size(), memres);
      arr.clear();
      for (std::string_view value : group.values) {
        std::optional<std::string_view> kept_value =
            budget.charge_string(value, true);
        if (!kept_value) {
          break;
        }
        arr.append().make_string(*kept_value);
      }
    }
    kept++;
    if (budget.exhausted()) {
      break;
    }
  }
  map.set_size_unchecked(kept);
  return true;
}

bool parse_body_req(ddwaf_obj &slot, const ngx_http_request_t &req,
                    const ngx_chain_t &chain, std::size_t size,
                    WafInputBudget &budget, DdwafMemres &memres) {
  if (req.headers_in.content_type == nullptr) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                  "no content-type: won't parse request body");
    return false;
  }

  if (is_req_json(req)) {
    // use rapidjson to parse:
    bool success = parse_json(slot, req, chain, size, budget, memres);
    if (success) {
      return true;
    }
  } else if (is_req_multipart(req)) {
    std::optional<HttpContentType> ct = HttpContentType::for_string(
        to_string_view(req.headers_in.content_type->value));
    if (!ct) {
      ngx_log_error(NGX_LOG_NOTICE, req.connection->log, 0,
                    "multipart: invalid multipart/form-data content-type");
      return false;
    }

    return parse_multipart(slot, req, *ct, chain, size, budget, memres);
  }

  if (is_req_text_plain(req)) {
    return parse_plain(slot, chain, size, budget, memres);
  }

  if (is_req_urlencoded(req)) {
    return parse_urlencoded(slot, chain, size, budget, memres);
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                "unsupported content-type: %V",
                &req.headers_in.content_type->value);
  return false;
}

bool is_body_resp_parseable(const ngx_http_request_t &req) {
  return !req.header_only && (is_resp_json(req) || is_resp_text_plain(req));
}

// chain may be longer than size, so size can act as a limit too
// size (size/limit) <= chain size
bool parse_body_resp(ddwaf_obj &slot, const ngx_http_request_t &req,
                     const ngx_chain_t &chain, std::size_t size,
                     WafInputBudget &budget, DdwafMemres &memres) {
  if (is_resp_json(req)) {
    return parse_json(slot, req, chain, size, budget, memres);
  }

  if (is_resp_text_plain(req)) {
    return parse_plain(slot, chain, size, budget, memres);
  }

  return false;
}

}  // namespace datadog::nginx::security
