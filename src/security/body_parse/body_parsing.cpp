#include "body_parsing.h"

#include <ddwaf.h>
#include <ngx_string.h>

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
  dnsec::NgxChainInputStream stream{&chain};
  auto read = stream.read(reinterpret_cast<std::uint8_t *>(buf), size);
  if (read < size) {
    throw std::runtime_error(
        "mismatch between declared size and read size (read is smaller than "
        "declared)");
  }
  return buf;
}

bool parse_plain(dnsec::ddwaf_obj &slot, const ngx_chain_t &chain,
                 std::size_t size, dnsec::DdwafMemres &memres) {
  char *buf = linearize_chain(chain, size, memres);

  slot.make_string(std::string_view{buf, size});
  return true;
}

bool parse_urlencoded(dnsec::ddwaf_obj &slot, const ngx_chain_t &chain,
                      std::size_t size, dnsec::DdwafMemres &memres) {
  char *buf = linearize_chain(chain, size, memres);

  dnsec::QueryStringIter it{
      {buf, size}, memres, '&', dnsec::QueryStringIter::trim_mode::no_trim};

  // count key occurrences
  union count_or_ddobj {
    std::size_t count;
    dnsec::ddwaf_obj *dobj;
  };
  std::unordered_map<std::string_view, count_or_ddobj> key_index;
  for (; !it.ended(); ++it) {
    std::string_view cur_key = it.cur_key();
    key_index[cur_key].count++;
  }

  // allocate all ddwaf_obj, set keys
  dnsec::ddwaf_map_obj slot_map = slot.make_map(key_index.size(), memres);
  std::size_t i = 0;
  for (auto &&[key, count_or_arr] : key_index) {
    dnsec::ddwaf_obj &cur = slot_map.at_unchecked(i++);
    cur.set_key(key);
    if (count_or_arr.count == 1) {
      cur.make_string(""sv);  // to be filled later
    } else {
      cur.make_array(count_or_arr.count, memres);
      cur.nbEntries = 0;  // fixed later
    }
    count_or_arr.dobj = &cur;
  }

  // set values
  it.reset();
  for (it.reset(); !it.ended(); ++it) {
    auto [cur_key, cur_value] = *it;
    dnsec::ddwaf_obj &cur = *key_index.at(cur_key).dobj;
    if (cur.is_string()) {
      cur.make_string(cur_value);
    } else {
      dnsec::ddwaf_arr_obj &cur_arr = static_cast<dnsec::ddwaf_arr_obj &>(cur);
      cur_arr.at_unchecked(cur_arr.nbEntries++).make_string(cur_value);
    }
  }

  return true;
}

}  // namespace

namespace datadog::nginx::security {

bool parse_body_req(ddwaf_obj &slot, const ngx_http_request_t &req,
                    const ngx_chain_t &chain, std::size_t size,
                    DdwafMemres &memres) {
  if (is_req_json(req)) {
    // use rapidjson to parse:
    bool success = parse_json(slot, req, chain, size, memres);
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

    return parse_multipart(slot, req, *ct, chain, memres);
  }

  if (is_req_text_plain(req)) {
    return parse_plain(slot, chain, size, memres);
  }

  if (is_req_urlencoded(req)) {
    return parse_urlencoded(slot, chain, size, memres);
  }

  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                 "unsupported content-type: %V",
                 &req.headers_in.content_type->value);
  return false;
}

bool is_body_resp_parseable(const ngx_http_request_t &req) {
  return !req.header_only && req.headers_out.content_length_n != 0 &&
         (is_resp_json(req) || is_resp_text_plain(req));
}

// chain may be longer than size, so size can act as a limit too
// the limit can't be smaller than the size of the chain though
bool parse_body_resp(ddwaf_obj &slot, const ngx_http_request_t &req,
                     const ngx_chain_t &chain, std::size_t size,
                     DdwafMemres &memres) {
  if (is_resp_json(req)) {
    return parse_json(slot, req, chain, size, memres);
  }

  if (is_resp_text_plain(req)) {
    return parse_plain(slot, chain, size, memres);
  }

  return false;
}

}  // namespace datadog::nginx::security
