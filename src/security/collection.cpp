#include "collection.h"

#include <ddwaf.h>

#include <cassert>
#include <cctype>
#include <charconv>
#include <cstring>
#include <string_view>
#include <unordered_map>

#include "ddwaf_obj.h"
#include "decode.h"
#include "util.h"

extern "C" {
#include <ngx_string.h>
}

namespace {

namespace dns = datadog::nginx::security;

template <typename T, typename = std::void_t<>>
struct has_cookie : std::false_type {};
template <typename T>
struct has_cookie<T, std::void_t<decltype(std::declval<T>().cookie)>>
    : std::true_type {};
static constexpr auto headers_in_has_cookie_v =
    has_cookie<decltype(ngx_http_request_t{}.headers_in)>::value;

class req_serializer {
  static constexpr std::string_view QUERY{"server.request.query"};
  static constexpr std::string_view URI_RAW{"server.request.uri.raw"};
  static constexpr std::string_view METHOD{"server.request.method"};
  static constexpr std::string_view HEADERS_NO_COOKIES{
      "server.request.headers.no_cookies"};
  static constexpr std::string_view COOKIES{"server.request.cookies"};
  static constexpr std::string_view STATUS{"server.response.status"};
  static constexpr std::string_view RESP_HEADERS_NO_COOKIES{
      "server.response.headers.no_cookies"};

 public:
  req_serializer(dns::ddwaf_memres &memres) : memres_{memres} {}

  ddwaf_object *serialize(const ngx_http_request_t &request) {
    dns::ddwaf_obj *root = memres_.allocate_objects<dns::ddwaf_obj>(1);
    dns::ddwaf_map_obj &root_map = root->make_map(5, memres_);

    set_request_query(request, root_map.get_entry_unchecked(0));
    set_request_uri_raw(request, root_map.get_entry_unchecked(1));
    set_request_method(request, root_map.get_entry_unchecked(2));
    set_request_headers_nocookies(request, root_map.get_entry_unchecked(3));
    set_request_cookie(request, root_map.get_entry_unchecked(4));

    return root;
  }

  ddwaf_object *serialize_end(const ngx_http_request_t &request) {
    dns::ddwaf_obj *root = memres_.allocate_objects<dns::ddwaf_obj>(1);
    dns::ddwaf_map_obj &root_map = root->make_map(2, memres_);

    set_response_status(request, root_map.get_entry_unchecked(0));
    set_response_headers_no_cookies(request, root_map.get_entry_unchecked(1));

    return root;
  }

 private:
  void set_map_entry_str(dns::ddwaf_obj &slot, std::string_view key,
                         const ngx_str_t &value) {
    slot.set_key(key);
    slot.make_string(dns::to_sv(value));
  }

  void set_request_query(const ngx_http_request_t &request,
                         dns::ddwaf_obj &slot) {
    slot.set_key(QUERY);
    const ngx_str_t &query = request.args;
    if (query.len == 0) {
      slot.make_array(nullptr, 0);
      return;
    }

    dns::query_string_iter it{query, memres_, '&',
                              dns::query_string_iter::trim_mode::no_trim};
    set_value_from_iter(it, slot);
  }

  template <typename Iter>
  void set_value_from_iter(Iter &it, dns::ddwaf_obj &slot) {
    // first, count the number of occurrences for each key
    std::unordered_map<std::string_view, std::size_t> keys_bag;
    for (; !it.ended(); ++it) {
      std::string_view key = it.cur_key();
      keys_bag[key]++;
    }

    // we now know the number of keys; allocate map entries
    dns::ddwaf_obj *entries =
        memres_.allocate_objects<dns::ddwaf_obj>(keys_bag.size());
    slot.make_map(entries, keys_bag.size());
    dns::ddwaf_obj *next_free_entry = entries;

    // fill the map entries
    // map that saves the ddwaf_object for keys that occurr more than once
    std::unordered_map<std::string_view, dns::ddwaf_arr_obj *> indexed_entries;
    for (it.reset(); !it.ended(); ++it) {
      auto [key, value] = *it;
      std::size_t num_occurr = keys_bag[key];

      // common scenario: only 1 occurrence of the key
      if (num_occurr == 1) {
        dns::ddwaf_obj &entry = *next_free_entry++;
        entry.set_key(key);
        entry.make_string(value);
        continue;
      }

      auto ie = indexed_entries.find(key);
      if (ie == indexed_entries.end()) {
        // first occurrence of this key
        dns::ddwaf_obj &entry = *next_free_entry++;

        entry.set_key(key);
        auto &arr_val = entry.make_array(num_occurr, memres_);
        arr_val.at_unchecked<dns::ddwaf_obj>(0).make_string(value);
        entry.nbEntries = 1;

        indexed_entries.insert({key, &arr_val});
      } else {
        // subsequence occurrence of this key
        auto &arr_val = *ie->second;
        arr_val.template at_unchecked<dns::ddwaf_obj>(arr_val.nbEntries++)
            .make_string(value);
      }
    }
  }

  void set_request_uri_raw(const ngx_http_request_t &request,
                           dns::ddwaf_obj &slot) {
    set_map_entry_str(slot, URI_RAW, request.unparsed_uri);
  }

  void set_request_method(const ngx_http_request_t &request,
                          dns::ddwaf_obj &slot) {
    set_map_entry_str(slot, METHOD, request.method_name);
  }

  using ngx_str_bag =
      std::unordered_map<ngx_str_t, int, dns::ngx_str_hash, dns::ngx_str_equal>;
  ngx_str_bag count_headers(const ngx_list_t &list) {
    ngx_str_bag ret;
    dns::ngnix_header_iterable it{list};
    for (auto &&h : it) {
      // ignore the case where h.hash == 0 (header deleted), but do not erase
      // the key. At this point, we write the headers as we find them, even if
      // we later delete them. We need space allocated for this purpose.
      // If we have value1, value2, <delete>, value3, at the end we only need
      // space for one value. However, we write value1 and value2 before finding
      // the delete so we need space for 2 actually. Our count will actually
      // give space for 3, but that's fine.
      if (h.hash == 0) {
        continue;
      }
      ret[ngx_str_t{.len = h.key.len, .data = h.lowcase_key}] += 1;
    }
    return ret;
  }
  static std::size_t tally_headers_no_cookies(const ngx_str_bag &bag) {
    static const ngx_str_t cookie = {.len = sizeof("cookie") - 1,
                                     .data = (u_char *)"cookie"};
    if (bag.find(cookie) == bag.end()) {
      return bag.size();
    } else {
      return bag.size() - 1;
    }
  }
  static std::size_t tally_resp_headers_no_cookies(const ngx_str_bag &bag) {
    static const ngx_str_t cookie = {.len = sizeof("set-cookie") - 1,
                                     .data = (u_char *)"set-cookie"};
    if (bag.find(cookie) == bag.end()) {
      return bag.size();
    } else {
      return bag.size() - 1;
    }
  }
  using ngx_str_dobj_arr =
      std::unordered_map<ngx_str_t, dns::ddwaf_obj *, dns::ngx_str_hash,
                         dns::ngx_str_equal>;
  auto alloc_multivalue_header_arr(const ngx_str_bag &headers) {
    ngx_str_dobj_arr ret;
    for (auto it = headers.begin(); it != headers.end(); it++) {
      if (it->second > 1) {
        ret[it->first] = memres_.allocate_objects<dns::ddwaf_obj>(it->second);
      }
    }
    return ret;
  }

  void set_request_headers_nocookies(const ngx_http_request_t &request,
                                     dns::ddwaf_obj &slot) {
    set_headers_generic(request, slot, HEADERS_NO_COOKIES,
                        tally_headers_no_cookies);
  }

  template <typename TallyFunc>
  void set_headers_generic(const ngx_http_request_t &request,
                           dns::ddwaf_obj &slot, std::string_view key,
                           TallyFunc &&tally_func) {
    slot.set_key(key);

    ngx_str_bag header_bag = count_headers(request.headers_out.headers);
    std::size_t header_count =
        std::invoke(std::forward<TallyFunc>(tally_func), header_bag);

    slot.make_map(header_count, memres_);
    dns::ddwaf_obj *entries = static_cast<dns::ddwaf_obj *>(slot.array);

    ngx_str_dobj_arr multivalue_arr = alloc_multivalue_header_arr(header_bag);
    dns::ngnix_header_iterable it{request.headers_out.headers};
    std::size_t i = 0;
    for (auto &&h : it) {
      assert(h.lowcase_key != nullptr);  // TODO

      ngx_str_t key{h.key.len, h.lowcase_key};
      auto count = header_bag[key];

      dns::ddwaf_obj &dobj = *entries++;
      dobj.set_key({reinterpret_cast<char *>(h.lowcase_key), h.key.len});
      if (count == 1) {
        if (h.hash == 0) {
          continue;
        }
        dobj.make_string(dns::to_sv(h.value));
      } else {
        if (h.hash == 0) {
          dobj.nbEntries = 0;
          continue;
        }
        ngx_str_t lowcase_key{h.key.len, h.lowcase_key};
        dns::ddwaf_obj *arr = multivalue_arr[lowcase_key];

        dobj.type = DDWAF_OBJ_ARRAY;
        dobj.array = arr;
        // dobj is zeroed, so nbEntries is 0 the first time

        ddwaf_object &dobj_ae = arr[dobj.nbEntries++];
        dobj_ae.type = DDWAF_OBJ_STRING;
        dobj_ae.stringValue = (char *)h.value.data;
        dobj_ae.nbEntries = h.value.len;
      }
    }
  }

  template <typename Request = ngx_http_request_t>
  void set_request_cookie(const Request &request, dns::ddwaf_obj &slot) {
    slot.set_key(COOKIES);

    dns::qs_iter_agg iter{};

    if constexpr (headers_in_has_cookie_v) {
      auto *t = request.headers_in.cookie;
      std::size_t count = 0;
      for (auto tp = t; tp; tp = tp->next) {
        assert(tp->hash != 0);
        count++;
      }

      iter.iters.reserve(count);

      for (auto tp = t; tp; tp = tp->next) {
        iter.add(std::make_unique<dns::query_string_iter>(
            dns::to_sv(tp->value), memres_, ';',
            dns::query_string_iter::trim_mode::do_trim));
      }
    } else {
      std::vector<const ngx_table_elt_t *> cookie_headers;
      dns::ngnix_header_iterable it{request.headers_in.headers};
      for (auto &&h : it) {
        static constexpr std::string_view COOKIE{"cookie"};
        if (std::string_view{reinterpret_cast<char *>(h.lowcase_key),
                             h.key.len} != COOKIE) {
          continue;
        }
        if (h.hash == 0) {
          cookie_headers.clear();
          continue;
        }
        cookie_headers.push_back(&h);
      }

      for (auto &&ch : cookie_headers) {
        iter.add(std::make_unique<dns::query_string_iter>(
            dns::to_sv(ch->value), memres_, ';',
            dns::query_string_iter::trim_mode::do_trim));
      }
    }

    if (iter.ended()) {
      slot.make_map(nullptr, 0);
      return;
    }

    set_value_from_iter(iter, slot);
  }

  void set_response_status(const ngx_http_request_t &request,
                           dns::ddwaf_obj &slot) const {
    slot.set_key(STATUS);

    // use the stastus line rather than the status number in order to avoid
    // having to allocate space for a string
    std::string_view sv{dns::to_sv(request.headers_out.status_line)};

    // find the first space
    while (!sv.empty() && !std::isspace(sv.front())) {
      sv.remove_prefix(1);
    }

    // find the end of numeric characters
    std::size_t e;
    for (e = 0; e < sv.length(); e++) {
      char c = sv[e];
      if (c < '0' || c > '9') {
        break;
      }
    }

    // remove eveything in position e and following
    sv.remove_suffix(sv.length() - e);

    slot.make_string(sv);
  }

  void set_response_headers_no_cookies(const ngx_http_request_t &request,
                                       dns::ddwaf_obj &slot) {
    set_headers_generic(request, slot, RESP_HEADERS_NO_COOKIES,
                        tally_resp_headers_no_cookies);
  }

  dns::ddwaf_memres &memres_;
};

}  // namespace

namespace datadog {
namespace nginx {
namespace security {

ddwaf_object *collect_request_data(const ngx_http_request_t &request,
                                   ddwaf_memres &memres) {
  req_serializer rs{memres};
  return rs.serialize(request);
}

ddwaf_object *collect_response_data(const ngx_http_request_t &request,
                                    ddwaf_memres &memres) {
  req_serializer rs{memres};
  return rs.serialize_end(request);
}
}  // namespace security
}  // namespace nginx
}  // namespace datadog
