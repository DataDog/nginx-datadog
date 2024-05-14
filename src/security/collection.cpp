#include "collection.h"

#include <ddwaf.h>

#include <cassert>
#include <cctype>
#include <charconv>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>

#include "../string_util.h"
#include "client_ip.h"
#include "ddwaf_obj.h"
#include "decode.h"
#include "library.h"
#include "util.h"

extern "C" {
#include <ngx_list.h>
#include <ngx_string.h>
}

using namespace std::literals;
using datadog::nginx::to_string_view;

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)

namespace {

namespace dnsec = datadog::nginx::security;

template <typename T, typename = std::void_t<>>
struct HasCookie : std::false_type {};
template <typename T>
struct HasCookie<T, std::void_t<decltype(std::declval<T>().cookie)>>
    : std::true_type {};
constexpr auto kHeadersInHasCookieV =
    HasCookie<decltype(ngx_http_request_t{}.headers_in)>::value;

class ReqSerializer {
  static constexpr std::string_view kQuery{"server.request.query"};
  static constexpr std::string_view kUriRaw{"server.request.uri.raw"};
  static constexpr std::string_view kMethod{"server.request.method"};
  static constexpr std::string_view kHeadersNoCookies{
      "server.request.headers.no_cookies"};
  static constexpr std::string_view kCookies{"server.request.cookies"};
  static constexpr std::string_view kStatus{"server.response.status"};
  static constexpr std::string_view kClientIp{"http.client_ip"};
  static constexpr std::string_view kRespHeadersNoCookies{
      "server.response.headers.no_cookies"};

 public:
  explicit ReqSerializer(dnsec::DdwafMemres &memres) : memres_{memres} {}

  ddwaf_object *serialize(const ngx_http_request_t &request) {
    dnsec::ddwaf_obj *root = memres_.allocate_objects<dnsec::ddwaf_obj>(1);
    dnsec::ddwaf_map_obj &root_map = root->make_map(6, memres_);

    set_request_query(request, root_map.at_unchecked(0));
    set_request_uri_raw(request, root_map.at_unchecked(1));
    set_request_method(request, root_map.at_unchecked(2));
    set_request_headers_nocookies(request, root_map.at_unchecked(3));
    set_request_cookie(request, root_map.at_unchecked(4));
    set_client_ip(request, root_map.at_unchecked(5));

    return root;
  }

  ddwaf_object *serialize_end(const ngx_http_request_t &request) {
    dnsec::ddwaf_obj *root = memres_.allocate_objects<dnsec::ddwaf_obj>(1);
    dnsec::ddwaf_map_obj &root_map = root->make_map(2, memres_);

    set_response_status(request, root_map.at_unchecked(0));
    set_response_headers_no_cookies(request, root_map.at_unchecked(1));

    return root;
  }

 private:
  static void set_map_entry_str(dnsec::ddwaf_obj &slot, std::string_view key,
                                const ngx_str_t &value) {
    slot.set_key(key);
    slot.make_string(to_string_view(value));
  }

  void set_request_query(const ngx_http_request_t &request,
                         dnsec::ddwaf_obj &slot) {
    slot.set_key(kQuery);
    const ngx_str_t &query = request.args;
    if (query.len == 0) {
      slot.make_array(nullptr, 0);
      return;
    }

    dnsec::QueryStringIter it{query, memres_, '&',
                              dnsec::QueryStringIter::trim_mode::no_trim};
    set_value_from_iter(it, slot);
  }

  template <typename Iter>
  void set_value_from_iter(Iter &it, dnsec::ddwaf_obj &slot) {
    // first, count the number of occurrences for each key
    std::unordered_map<std::string_view, std::size_t> keys_bag;
    for (; !it.ended(); ++it) {
      std::string_view key = it.cur_key();
      // if (it.is_delete()) { }
      // don't reduce the count, we need to allocate space for the provisional
      // writes (before the deletion)
      // we could improve this by saving the pointer of the first non-deleted
      // header per key
      keys_bag[key]++;
    }

    // we now know the number of keys; allocate map entries
    dnsec::ddwaf_obj *entries =
        memres_.allocate_objects<dnsec::ddwaf_obj>(keys_bag.size());
    slot.make_map(entries, keys_bag.size());
    dnsec::ddwaf_obj *next_free_entry = entries;

    // fill the map entries
    // map that saves the ddwaf_object for keys that occurr more than once
    std::unordered_map<std::string_view, dnsec::ddwaf_arr_obj *>
        indexed_entries;
    for (it.reset(); !it.ended(); ++it) {
      auto [key, value] = *it;
      std::size_t const num_occurr = keys_bag[key];

      // common scenario: only 1 occurrence of the key
      if (num_occurr == 1) {
        dnsec::ddwaf_obj &entry = *next_free_entry++;
        entry.set_key(key);
        entry.make_string(value);
        continue;
      }

      auto ie = indexed_entries.find(key);
      if (ie == indexed_entries.end()) {
        // first occurrence of this key
        dnsec::ddwaf_obj &entry = *next_free_entry++;

        entry.set_key(key);
        auto &arr_val = entry.make_array(num_occurr, memres_);
        indexed_entries.insert({key, &arr_val});

        if (!it.is_delete()) {
          arr_val.at_unchecked<dnsec::ddwaf_obj>(0).make_string(value);
          entry.nbEntries = 1;
        }
      } else {
        // subsequent occurrence of this key
        auto &arr_val = *ie->second;
        if (!it.is_delete()) {
          arr_val.template at_unchecked<dnsec::ddwaf_obj>(arr_val.nbEntries++)
              .make_string(value);
        } else {
          arr_val.nbEntries = 0;
        }
      }
    }
  }

  static void set_request_uri_raw(const ngx_http_request_t &request,
                                  dnsec::ddwaf_obj &slot) {
    set_map_entry_str(slot, kUriRaw, request.unparsed_uri);
  }

  static void set_request_method(const ngx_http_request_t &request,
                                 dnsec::ddwaf_obj &slot) {
    set_map_entry_str(slot, kMethod, request.method_name);
  }

  // adapt to the same iterator format as query_string_iter
  template <bool IsRequest>
  struct HeaderKeyValueIter {
    HeaderKeyValueIter(const ngx_list_t &list, std::string_view exclude,
                       dnsec::DdwafMemres &memres)
        : list_{list},
          memres_{memres},
          exclude_{exclude},
          it_{list},
          end_{dnsec::NginxListIter<ngx_table_elt_t>::end(list)} {}

    void reset() { it_ = decltype(it_){list_}; }

    bool ended() const noexcept { return it_ == end_; }

    HeaderKeyValueIter &operator++() {
      while (true) {
        ++it_;
        if (it_ == end_) {
          break;
        }
        const auto &h = *it_;
        auto lc_key = safe_lowcase_key(h);
        if (lc_key != exclude_) {
          break;
        }
        // then it's the excluded key; continue
      }
      return *this;
    }

    std::string_view cur_key() {
      const auto &h = *it_;
      return safe_lowcase_key(h);
    }

    std::pair<std::string_view, std::string_view> operator*() {
      const auto &h = *it_;
      return {safe_lowcase_key(h), to_string_view(h.value)};
    }

    bool is_delete() const {
      if constexpr (IsRequest) {
        return false;
      } else {  // response headers
        const auto &h = *it_;
        return h.hash == 0;
      }
    }

    std::string_view safe_lowcase_key(const ngx_table_elt_t &header) {
      if constexpr (IsRequest) {
        return dnsec::lc_key(header);
      }

      // impl for response headers
      auto key = to_string_view(header.key);
      auto it = lc_keys_.find(key);
      if (it != lc_keys_.end()) {
        return it->second;
      }

      auto *lc_out_buffer =
          reinterpret_cast<u_char *>(memres_.allocate_string(header.key.len));
      std::transform(header.key.data, header.key.data + header.key.len,
                     lc_out_buffer, [](u_char c) {
                       if (c >= 'A' && c <= 'Z') {
                         return static_cast<u_char>(c + ('a' - 'A'));
                       }
                       return c;
                     });

      std::string_view lc_sv{reinterpret_cast<char *>(lc_out_buffer),
                             header.key.len};
      lc_keys_.insert({key, lc_sv});
      return lc_sv;
    }

   private:
    const ngx_list_t &list_;
    dnsec::DdwafMemres &memres_;
    std::string_view exclude_;
    std::unordered_map<std::string_view, std::string_view> lc_keys_;
    dnsec::NginxListIter<ngx_table_elt_t> it_;
    dnsec::NginxListIter<ngx_table_elt_t> end_;
  };

  void set_request_headers_nocookies(const ngx_http_request_t &request,
                                     dnsec::ddwaf_obj &slot) {
    static constexpr auto cookie = "cookie"sv;
    slot.set_key(kHeadersNoCookies);
    HeaderKeyValueIter<true> it{request.headers_in.headers, cookie, memres_};
    set_value_from_iter(it, slot);
  }

  template <typename Request = ngx_http_request_t>
  void set_request_cookie(const Request &request, dnsec::ddwaf_obj &slot) {
    slot.set_key(kCookies);

    dnsec::qs_iter_agg iter{};

    if constexpr (kHeadersInHasCookieV) {
      auto *t = request.headers_in.cookie;
      std::size_t count = 0;
      for (auto tp = t; tp; tp = tp->next) {
        assert(tp->hash != 0);
        count++;
      }

      iter.iters.reserve(count);

      for (auto tp = t; tp; tp = tp->next) {
        iter.add(std::make_unique<dnsec::QueryStringIter>(
            to_string_view(tp->value), memres_, ';',
            dnsec::QueryStringIter::trim_mode::do_trim));
      }
    } else {
      std::vector<const ngx_table_elt_t *> cookie_headers;
      dnsec::NgnixHeaderIterable it{request.headers_in.headers};
      for (auto &&h : it) {
        if (!dnsec::req_key_equals_ci(h, "cookie"sv)) {
          continue;
        }
        cookie_headers.push_back(&h);
      }

      for (auto &&ch : cookie_headers) {
        iter.add(std::make_unique<dnsec::QueryStringIter>(
            to_string_view(ch->value), memres_, ';',
            dnsec::QueryStringIter::trim_mode::do_trim));
      }
    }

    if (iter.ended()) {
      slot.make_map(nullptr, 0);
      return;
    }

    set_value_from_iter(iter, slot);
  }

  void set_client_ip(const ngx_http_request_t &request,
                     dnsec::ddwaf_obj &slot) {
    dnsec::ClientIp client_ip{dnsec::Library::custom_ip_header(), request};
    std::optional<std::string> cl_ip = client_ip.resolve();

    slot.set_key(kClientIp);
    if (!cl_ip) {
      slot.make_null();
    }
    slot.make_string(*cl_ip, memres_);  // copy
  }

  void set_response_status(const ngx_http_request_t &request,
                           dnsec::ddwaf_obj &slot) {
    slot.set_key(kStatus);

    // generally status_line is not set so we can't use it to avoid a string
    // allocation. So don't bother
    auto status = request.headers_out.status;
    switch (status) {
      case 200:
        slot.make_string("200"sv);
        return;
      case 404:
        slot.make_string("404"sv);
        return;
      case 301:
        slot.make_string("301"sv);
        return;
      case 302:
        slot.make_string("302"sv);
        return;
      case 303:
        slot.make_string("303"sv);
        return;
      case 201:
        slot.make_string("201"sv);
        return;
      default:
        if (status < 100 || status > 599) {
          slot.make_string("0"sv);
          return;
        }
        char *s = memres_.allocate_string(3);
        s[2] = status % 10 + '0';
        status /= 10;
        s[1] = status % 10 + '0';
        s[0] = status / 10 + '0';
        slot.make_string({s, 3});
    }
  }

  void set_response_headers_no_cookies(const ngx_http_request_t &request,
                                       dnsec::ddwaf_obj &slot) {
    static constexpr auto set_cookie = "set-cookie"sv;
    slot.set_key(kRespHeadersNoCookies);
    HeaderKeyValueIter<false> it{request.headers_out.headers, set_cookie,
                                 memres_};
    set_value_from_iter(it, slot);
  }

  dnsec::DdwafMemres &memres_;
};

}  // namespace

namespace datadog::nginx::security {

ddwaf_object *collect_request_data(const ngx_http_request_t &request,
                                   DdwafMemres &memres) {
  ReqSerializer rs{memres};
  return rs.serialize(request);
}

ddwaf_object *collect_response_data(const ngx_http_request_t &request,
                                    DdwafMemres &memres) {
  ReqSerializer rs{memres};
  return rs.serialize_end(request);
}
}  // namespace datadog::nginx::security

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
