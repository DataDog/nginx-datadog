#include "collection.h"

#include <ddwaf.h>

#include <cassert>
#include <cstring>
#include <string_view>
#include <unordered_map>

extern "C" {
#include <ngx_string.h>
}

namespace {

using datadog::nginx::security::ddwaf_memres;

struct ngx_str_hash {
  std::size_t operator()(const ngx_str_t &str) const noexcept {
    // could use ngx_hash_key(str.data, str.len) but this way it can be inlined
    std::size_t hash = 5381;
    for (std::size_t i = 0; i < str.len; ++i) {
      hash = ((hash << 5) + hash) + str.data[i];  // hash * 33 + c
    }
    return hash;
  }
};

struct ngx_str_equal {
  bool operator()(const ngx_str_t &lhs, const ngx_str_t &rhs) const {
    if (lhs.len != rhs.len) {
      return false;
    }
    return std::memcmp(lhs.data, rhs.data, lhs.len) == 0;
  }
};

template <typename T>
class nginx_list_iter {
 public:
  explicit nginx_list_iter(const ngx_list_t &list)
      : nginx_list_iter{list.last} {}

  nginx_list_iter(ngx_list_part_t *part)
      : part_{part},
        elts_{static_cast<T *>(part_ ? part_->elts : nullptr)},
        index_{0} {}

  bool operator!=(const nginx_list_iter &other) const {
    return part_ != other.part_ || index_ != other.index_;
  }

  nginx_list_iter &operator++() {
    if (!part_) {
      return *this;
    }

    ++index_;
    if (index_ >= part_->nelts) {
      part_ = part_->next;
      elts_ = static_cast<T *>(part_ ? part_->elts : nullptr);
      index_ = 0;
    }
    return *this;
  }

  T &operator*() { return elts_[index_]; }

  static nginx_list_iter<T> end() { return nginx_list_iter{nullptr}; }

 private:
  ngx_list_part_t *part_;
  T *elts_;  // part_->elts, after cast
  ngx_uint_t index_;
};

class ngnix_header_iter {
 public:
  explicit ngnix_header_iter(const ngx_list_t &list) : list_{list} {}

  nginx_list_iter<ngx_table_elt_t> begin() {
    return nginx_list_iter<ngx_table_elt_t>{list_};
  }

  nginx_list_iter<ngx_table_elt_t> end() {
    return nginx_list_iter<ngx_table_elt_t>::end();
  }

 private:
  const ngx_list_t &list_;
};

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

 public:
  req_serializer(ddwaf_memres &memres) : memres_{memres} {}

  ddwaf_object *serialize(const ngx_http_request_t &request) {
    ddwaf_object *root = memres_.allocate_objects(1);
    root->type = DDWAF_OBJ_MAP;
    root->nbEntries = 5;
    root->array = memres_.allocate_objects(5);

    ddwaf_object *arr = root->array;

    set_request_query(request, root[0]);
    set_request_uri_raw(request, root[1]);
    set_request_method(request, root[2]);
    set_request_headers_nocookies(request, root[3]);
    set_request_cookie(request, root[4]);

    return root;
  }

 private:
  void set_map_entry_str(ddwaf_object &slot, std::string_view key,
                         const ngx_str_t &value) {
    slot.parameterName = key.data();
    slot.parameterNameLength = key.size();
    slot.type = DDWAF_OBJ_STRING;
    slot.stringValue = reinterpret_cast<const char *>(value.data);
    slot.nbEntries = value.len;
  }

  void set_request_query(const ngx_http_request_t &request,
                         ddwaf_object &slot) {
    // FIXME: args contains more than the query string
    set_map_entry_str(slot, QUERY, request.args);
  }

  void set_request_uri_raw(const ngx_http_request_t &request,
                           ddwaf_object &slot) {
    set_map_entry_str(slot, URI_RAW, request.uri);
  }

  void set_request_method(const ngx_http_request_t &request,
                          ddwaf_object &slot) {
    set_map_entry_str(slot, METHOD, request.method_name);
  }

  using ngx_str_bag =
      std::unordered_map<ngx_str_t, int, ngx_str_hash, ngx_str_equal>;
  ngx_str_bag count_headers(const ngx_list_t &list) {
    ngx_str_bag ret;
    ngnix_header_iter it{list};
    for (auto &&h : it) {
      if (h.hash == 0) {
        ret.erase(h.key);
      }
      ret[ngx_str_t{.len = h.key.len, .data = h.lowcase_key}] += 1;
    }
    return ret;
  }
  std::size_t tally_headers_no_cookies(const ngx_str_bag &bag) {
    static const ngx_str_t cookie = {.len = sizeof("cookie") - 1,
                                     .data = (u_char *)"cookie"};
    if (bag.find(cookie) == bag.end()) {
      return bag.size();
    } else {
      return bag.size() - 1;
    }
  }
  using ngx_str_dobj_arr = std::unordered_map<ngx_str_t, ddwaf_object *,
                                              ngx_str_hash, ngx_str_equal>;
  auto alloc_multivalue_header_arr(const ngx_str_bag &headers) {
    ngx_str_dobj_arr ret;
    for (auto it = headers.begin(); it != headers.end(); it++) {
      if (it->second > 1) {
        ret[it->first] = memres_.allocate_objects(it->second);
      }
    }
    return ret;
  }

  void set_request_headers_nocookies(const ngx_http_request_t &request,
                                     ddwaf_object &slot) {
    slot.type = DDWAF_OBJ_MAP;

    ngx_str_bag header_bag = count_headers(request.headers_in.headers);
    std::size_t header_count = tally_headers_no_cookies(header_bag);
    slot.nbEntries = header_count;

    slot.parameterName = "server.request.headers.no_cookies";
    slot.parameterNameLength = sizeof("server.request.headers.no_cookies") - 1;
    slot.array = memres_.allocate_objects(header_count);

    ngx_str_dobj_arr multivalue_arr = alloc_multivalue_header_arr(header_bag);
    ngnix_header_iter it{request.headers_in.headers};
    std::size_t i = 0;
    for (auto &&h : it) {
      assert(h.lowcase_key != nullptr);  // TODO

      ngx_str_t key{h.key.len, h.lowcase_key};
      auto count = header_bag[key];

      ddwaf_object &dobj = slot.array[i++];
      dobj.parameterName = (char *)h.lowcase_key;
      dobj.parameterNameLength = h.key.len;
      if (count == 1) {
        if (h.hash == 0) {
          continue;
        }
        dobj.type = DDWAF_OBJ_STRING;
        dobj.stringValue = (char *)h.value.data;
        dobj.nbEntries = h.value.len;
      } else {
        if (h.hash == 0) {
          dobj.nbEntries = 0;
          continue;
        }
        ngx_str_t lowcase_key{h.key.len, h.lowcase_key};
        ddwaf_object *arr = multivalue_arr[lowcase_key];

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
  void set_request_cookie(const Request &request, ddwaf_object &slot) {
    slot.parameterName = "server.request.cookies";
    slot.parameterNameLength = sizeof("server.request.cookies") - 1;

    if constexpr (headers_in_has_cookie_v) {
      auto *t = request.headers_in.cookie;
      std::size_t count = 0;
      for (auto tp = t; tp; tp = tp->next) {
        assert(tp->hash != 0);
        count++;
      }

      if (count == 1) {
        slot.type = DDWAF_OBJ_STRING;
        slot.stringValue = (char *)t->value.data;
        slot.nbEntries = t->value.len;
      } else {
        slot.type = DDWAF_OBJ_ARRAY;
        slot.nbEntries = count;
        slot.array = memres_.allocate_objects(count);
        for (std::size_t i = 0; i < count; i++) {
          slot.array[i].type = DDWAF_OBJ_STRING;
          // XXX: cookies need decoding
          slot.array[i].stringValue = (char *)t->value.data;
          slot.array[i].nbEntries = t->value.len;
          t = t->next;
        }
      }
    } else {
      std::vector<const ngx_table_elt_t *> cookie_headers;
      ngnix_header_iter it{request.headers_in.headers};
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

      if (cookie_headers.empty()) {
        slot.type = DDWAF_OBJ_MAP;
        slot.nbEntries = 0;
        return;
      }

      // XXX: cookies need decoding
      slot.type = DDWAF_OBJ_ARRAY;
      slot.nbEntries = cookie_headers.size();
      slot.array = memres_.allocate_objects(cookie_headers.size());
      for (std::size_t i = 0; i < cookie_headers.size(); i++) {
        slot.array[i].type = DDWAF_OBJ_STRING;
        slot.array[i].stringValue = (char *)cookie_headers[i]->value.data;
        slot.array[i].nbEntries = cookie_headers[i]->value.len;
      }
    }
  }

  ddwaf_memres &memres_;
};

}  // namespace

namespace datadog {
namespace nginx {

ddwaf_object *collect_request_data(const ngx_http_request_t &request,
                                   ddwaf_memres &memres) {
  req_serializer rs{memres};
  return rs.serialize(request);
}

}  // namespace nginx
}  // namespace datadog
