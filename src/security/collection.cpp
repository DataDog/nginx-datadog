#include "collection.h"

#include <ddwaf.h>

#include <cassert>
#include <cctype>
#include <charconv>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>

#include "ddwaf_obj.h"
#include "decode.h"
#include "util.h"

extern "C" {
#include <ngx_list.h>
#include <ngx_string.h>
}

using namespace std::literals;

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
      // if (it.is_delete()) { }
      // don't reduce the count, we need to allocate space for the provisional
      // writes (before the deletion)
      // we could improve this by saving the pointer of the first non-deleted
      // header per key
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
        indexed_entries.insert({key, &arr_val});

        if (!it.is_delete()) {
          arr_val.at_unchecked<dns::ddwaf_obj>(0).make_string(value);
          entry.nbEntries = 1;
        }
      } else {
        // subsequent occurrence of this key
        auto &arr_val = *ie->second;
        if (!it.is_delete()) {
          arr_val.template at_unchecked<dns::ddwaf_obj>(arr_val.nbEntries++)
              .make_string(value);
        } else {
          arr_val.nbEntries = 0;
        }
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

  // adapt to the same iteratror format as query_string_iter
  struct header_key_value_iter {
    header_key_value_iter(const ngx_list_t &list, std::string_view exclude,
                          dns::ddwaf_memres &memres)
        : list_{list},
          memres_{memres},
          exclude_{exclude},
          it_{list},
          end_{dns::nginx_list_iter<ngx_table_elt_t>::end(list)} {}

    void reset() { it_ = decltype(it_){list_}; }

    bool ended() const noexcept { return !(it_ != end_); }

    header_key_value_iter &operator++() {
      while (true) {
        ++it_;
        if (!(it_ != end_)) {
          break;
        }
        auto &&h = *it_;
        fill_lowcase_key(h);
        if (h.key.len != exclude_.length() ||
            !std::equal(h.lowcase_key, h.lowcase_key + h.key.len,
                        exclude_.begin())) {
          break;
        }
        // then it's the excluded key; continue
      }
      return *this;
    }

    std::string_view cur_key() {
      auto &&h = *it_;
      fill_lowcase_key(h);
      return {reinterpret_cast<char *>(h.lowcase_key), h.key.len};
    }

    std::pair<std::string_view, std::string_view> operator*() {
      auto &&h = *it_;
      return {{reinterpret_cast<char *>(h.lowcase_key), h.key.len},
              dns::to_sv(h.value)};
    }

    bool is_delete() const {
      auto &&h = *it_;
      return h.hash == 0;
    }

    void fill_lowcase_key(ngx_table_elt_t &header) {
      if (header.lowcase_key) {
        return;
      }
      header.lowcase_key =
          reinterpret_cast<u_char *>(memres_.allocate_string(header.key.len));
      std::transform(header.key.data, header.key.data + header.key.len,
                     header.lowcase_key, [](u_char c) {
                       if (c >= 'A' && c <= 'Z') {
                         return static_cast<u_char>(c + ('a' - 'A'));
                       }
                       return c;
                     });
    }

    const ngx_list_t &list_;
    dns::ddwaf_memres &memres_;
    std::string_view exclude_;
    dns::nginx_list_iter<ngx_table_elt_t> it_;
    dns::nginx_list_iter<ngx_table_elt_t> end_;
  };

  void set_request_headers_nocookies(const ngx_http_request_t &request,
                                     dns::ddwaf_obj &slot) {
    static constexpr auto cookie = "cookie"sv;
    slot.set_key(HEADERS_NO_COOKIES);
    header_key_value_iter it{request.headers_in.headers, cookie, memres_};
    set_value_from_iter(it, slot);
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
        assert(h.lowcase_key != nullptr);  // already filled

        static constexpr auto COOKIE{"cookie"sv};
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
                                       dns::ddwaf_obj &slot) {
    static constexpr auto set_cookie = "set-cookie"sv;
    slot.set_key(RESP_HEADERS_NO_COOKIES);
    header_key_value_iter it{request.headers_out.headers, set_cookie, memres_};
    set_value_from_iter(it, slot);
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
