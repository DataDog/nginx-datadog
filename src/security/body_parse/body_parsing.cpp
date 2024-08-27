#include "body_parsing.h"

#include <ddwaf.h>

#include <cstddef>
#include <unordered_map>

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
  while (sv.front() == ' ' || sv.front() == '\t') {
    sv.remove_prefix(1);
  }

  if (actual.size() < tested.size()) {
    return false;
  }

  for (std::size_t i = 0; i < tested.size(); i++) {
    if (std::tolower(sv.at(i)) != tested.at(i)) {
      return false;
    }
  }
  sv.remove_prefix(tested.size());

  if (!sv.empty()) {
    auto next = sv.front();
    if (next == ';' || next == ' ' || next == '\t') {
      return true;
    }
    return false;
  }

  return true;
}

bool is_json(const ngx_http_request_t &req) {
  const ngx_table_elt_t *ct = req.headers_in.content_type;
  // don't look at ct->next; consider only the first value
  return ct && is_content_type(datadog::nginx::to_string_view(ct->value),
                               "application/json"sv);
}

bool is_multipart(const ngx_http_request_t &req) {
  const ngx_table_elt_t *ct = req.headers_in.content_type;
  return ct && is_content_type(datadog::nginx::to_string_view(ct->value),
                               "multipart/form-data"sv);
}

bool is_urlencoded(const ngx_http_request_t &req) {
  const ngx_table_elt_t *ct = req.headers_in.content_type;
  return ct && is_content_type(datadog::nginx::to_string_view(ct->value),
                               "application/x-www-form-urlencoded"sv);
}

}  // namespace

namespace datadog::nginx::security {

bool parse_body(ddwaf_obj &slot, ngx_http_request_t &req,
                const ngx_chain_t &chain, std::size_t size,
                DdwafMemres &memres) {
  if (is_json(req)) {
    // use rapidjson to parse:
    bool success = parse_json(slot, req, chain, memres);
    if (success) {
      return true;
    }
  }

  if (is_multipart(req)) {
    std::optional<HttpContentType> ct = HttpContentType::for_string(
        to_string_view(req.headers_in.content_type->value));
    if (!ct) {
      ngx_log_error(NGX_LOG_NOTICE, req.connection->log, 0,
                    "multipart: invalid multipart/form-data content-type");
      return false;
    }

    return parse_multipart(slot, req, *ct, chain, memres);
  }

  // linearize the input
  char *buf = memres.allocate_string(size);
  NgxChainInputStream stream{&chain};
  auto read = stream.read(reinterpret_cast<std::uint8_t *>(buf), size);
  if (read < size) {
    throw std::runtime_error(
        "mismatch between declared size and read size (read is smaller than "
        "declared)");
  }

  if (is_urlencoded(req)) {
    QueryStringIter it{
        {buf, size}, memres, '&', QueryStringIter::trim_mode::no_trim};

    // count key occurrences
    std::unordered_map<std::string_view, std::size_t> bag;
    for (; !it.ended(); ++it) {
      std::string_view cur_key = it.cur_key();
      bag[cur_key]++;
    }

    // allocate all ddwaf_obj, set keys
    std::unordered_map<std::string_view, ddwaf_obj *> key_index;
    ddwaf_map_obj slot_map = slot.make_map(bag.size(), memres);
    std::size_t i = 0;
    for (auto &&[key, count] : bag) {
      ddwaf_obj &cur = slot_map.at_unchecked(i++);
      key_index.emplace(key, &cur);
      cur.set_key(key);
      if (count == 1) {
        cur.make_string(""sv);  // to be filled later
      } else {
        cur.make_array(count, memres);
        cur.nbEntries = 0;  // fixed later
      }
    }

    // set values
    it.reset();
    for (it.reset(); !it.ended(); ++it) {
      auto [cur_key, cur_value] = *it;
      ddwaf_obj &cur = *key_index.at(cur_key);
      if (cur.is_string()) {
        cur.make_string(cur_value);
      } else {
        ddwaf_arr_obj &cur_arr = static_cast<ddwaf_arr_obj &>(cur);
        cur_arr.at_unchecked(cur_arr.nbEntries++).make_string(cur_value);
      }
    }

    return true;
  }

  slot.make_string(std::string_view{buf, size});

  return true;
}

}  // namespace datadog::nginx::security
