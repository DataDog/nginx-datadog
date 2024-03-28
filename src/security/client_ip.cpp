#include "client_ip.h"

#include <array>
#include <string_view>

#include "util.h"

extern "C" {
#include <arpa/inet.h>
#include <ngx_config.h>
#include <ngx_hash.h>
#include <ngx_http_request.h>
#include <ngx_list.h>
}

using namespace std::literals;
namespace dns = datadog::nginx::security;
using datadog::nginx::to_string_view;

namespace {

struct ipaddr {
  int af;
  union {
    struct in_addr v4;
    struct in6_addr v6;
  } u;

  bool empty() const { return af == 0; }
  bool is_ipv4() const { return af == AF_INET; }
  bool is_ipv6() const { return af == AF_INET6; }

  std::optional<std::string> to_string() const {
    char buf[INET6_ADDRSTRLEN];
    if (inet_ntop(af, reinterpret_cast<const char *>(&this->u), buf,
                  sizeof(buf)) == nullptr) {
      return std::nullopt;
    }
    return {buf};
  };

  bool is_private() const {
    if (af == AF_INET) {
      return is_private_v4();
    } else {
      return is_private_v6();
    }
  }

  bool is_private_v4() const;
  bool is_private_v6() const;

  static std::optional<ipaddr> from_string(std::string_view addr_sv,
                                           int af_hint = 0);
};

std::optional<ipaddr> ipaddr::from_string(std::string_view addr_sv,
                                          int af_hint) {
  ipaddr out;
  std::string input_nulterminated{addr_sv};
  auto *addr_nult = input_nulterminated.c_str();

  if (af_hint != AF_INET6) {
    int ret = inet_pton(AF_INET, addr_nult, &out.u.v4);
    if (ret == 1) {
      out.af = AF_INET;
      return {out};
    }
  }

  int ret = inet_pton(AF_INET6, addr_nult, &out.u.v6);
  if (ret != 1) {
    // neither valid ipv4 nor ipv6
    return std::nullopt;
  }

  uint8_t *s6addr = out.u.v6.s6_addr;
  static constexpr uint8_t ip4_mapped_prefix[12] = {0, 0, 0, 0, 0,    0,
                                                    0, 0, 0, 0, 0xFF, 0xFF};
  if (std::memcmp(s6addr, ip4_mapped_prefix, sizeof(ip4_mapped_prefix)) == 0) {
    // IPv4 mapped
    if (af_hint == AF_INET6) {
      return std::nullopt;
    }
    std::memcpy(&out.u.v4.s_addr, s6addr + sizeof(ip4_mapped_prefix), 4);
    out.af = AF_INET;
  } else {
    if (af_hint == AF_INET) {
      return std::nullopt;
    }
    out.af = AF_INET6;
  }

  return {out};
}

inline constexpr auto ct_htonl(std::uint32_t n) {
#if defined(__BIG_ENDIAN__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  return n;
#else
  return __builtin_bswap32(n);
#endif
}
inline constexpr auto ct_htonll(std::uint64_t n) {
#if defined(__BIG_ENDIAN__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  return n;
#else
  return __builtin_bswap64(n);
#endif
}

bool ipaddr::is_private_v4() const {
  static constexpr struct {
    struct in_addr base;
    struct in_addr mask;
  } priv_ranges[] = {
      {
          .base = {ct_htonl(0x0A000000U)},  // 10.0.0.0
          .mask = {ct_htonl(0xFF000000U)},  // 255.0.0.0
      },
      {
          .base = {ct_htonl(0xAC100000U)},  // 172.16.0.0
          .mask = {ct_htonl(0xFFF00000U)},  // 255.240.0.0
      },
      {
          .base = {ct_htonl(0xC0A80000U)},  // 192.168.0.0
          .mask = {ct_htonl(0xFFFF0000U)},  // 255.255.0.0
      },
      {
          .base = {ct_htonl(0x7F000000U)},  // 127.0.0.0
          .mask = {ct_htonl(0xFF000000U)},  // 255.0.0.0
      },
      {
          .base = {ct_htonl(0xA9FE0000U)},  // 169.254.0.0
          .mask = {ct_htonl(0xFFFF0000U)},  // 255.255.0.0
      },
  };

  for (std::size_t i = 0; i < sizeof(priv_ranges) / sizeof(priv_ranges[0]);
       i++) {
    if ((u.v4.s_addr & priv_ranges[i].mask.s_addr) ==
        priv_ranges[i].base.s_addr) {
      return true;
    }
  }
  return false;
}

bool ipaddr::is_private_v6() const {
  static constexpr struct {
    union {
      struct in6_addr base;
      uint64_t base_i[2];
    };
    union {
      struct in6_addr mask;
      uint64_t mask_i[2];
    };
  } priv_ranges[] = {
      {
          .base_i = {0, ct_htonll(1ULL)},                           // loopback
          .mask_i = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL}  // /128
      },
      {
          .base_i = {ct_htonll(0xFE80ULL << 48), 0},  // link-local
          .mask_i = {ct_htonll(0xFFC0ULL << 48), 0}   // /10 mask
      },
      {
          .base_i = {ct_htonll(0xFEC0ULL << 48), 0},  // site-local
          .mask_i = {ct_htonll(0xFFC0ULL << 48), 0}   // /10 mask
      },
      {
          .base_i = {ct_htonll(0xFDULL << 56), 0},  // unique local address
          .mask_i = {ct_htonll(0xFFULL << 56), 0}   // /10 mask
      },
      {
          .base_i = {ct_htonll(0xFCULL << 56), 0},
          .mask_i = {ct_htonll(0xFEULL << 56), 0}  // /7
      },
  };

  uint64_t addr_i[2];
  std::memcpy(&addr_i[0], u.v6.s6_addr, sizeof(addr_i));

  for (std::size_t i = 0; i < sizeof(priv_ranges) / sizeof(priv_ranges[0]);
       i++) {
    if ((addr_i[0] & priv_ranges[i].mask_i[0]) == priv_ranges[i].base_i[0] &&
        (addr_i[1] & priv_ranges[i].mask_i[1]) == priv_ranges[i].base_i[1]) {
      return true;
    }
  }
  return false;
}

struct extract_res {
  bool success;
  bool is_private;

  static inline extract_res success_public() { return {true, false}; }
  static inline extract_res success_private() { return {true, true}; }
  static inline extract_res failure() { return {false}; }

  bool operator==(const extract_res &other) {
    return success && other.success && is_private == other.is_private ||
           !success && !other.success;
  }
};

struct ngx_table_elt_next_t : ngx_table_elt_t {
  ngx_table_elt_next_t(const ngx_table_elt_t &header)
      : ngx_table_elt_t{header} {}

  std::unique_ptr<ngx_table_elt_next_t> next{};
};

class header_proc_def {
 public:
  using extract_func_t = extract_res (*)(const ngx_table_elt_next_t &value,
                                         ipaddr &out);

  constexpr header_proc_def(std::string_view key, extract_func_t parse_func)
      : lc_key_{key},
        lc_key_hash_{dns::ngx_hash_ce(key)},
        parse_func_{parse_func} {}

  std::string_view lc_key_;
  ngx_uint_t lc_key_hash_;
  extract_func_t parse_func_;
};
// clang-format off
extract_res parse_multiple_maybe_port(const ngx_table_elt_next_t& value, ipaddr &out);
extract_res parse_multiple_maybe_port_sv(std::string_view sv, ipaddr &out);
extract_res parse_forwarded(const ngx_table_elt_next_t& value, ipaddr &out);
extract_res parse_forwarded_sv(std::string_view sv, ipaddr &out);
std::optional<ipaddr> parse_ip_address_maybe_port_pair(std::string_view sv);
// clang-format on

static constexpr auto priority_header_arr = std::array<header_proc_def, 10>{
    header_proc_def{"x-forwarded-for"sv, parse_multiple_maybe_port},
    {"x-real-ip"sv, parse_multiple_maybe_port},
    {"true-client-ip"sv, parse_multiple_maybe_port},
    {"x-client-ip"sv, parse_multiple_maybe_port},
    {"x-forwarded"sv, parse_forwarded},
    {"forwarded-for"sv, parse_multiple_maybe_port},
    {"x-cluster-client-ip"sv, parse_multiple_maybe_port},
    {"fastly-client-ip"sv, parse_multiple_maybe_port},
    {"cf-connecting-ip"sv, parse_multiple_maybe_port},
    {"cf-connecting-ipv6"sv, parse_multiple_maybe_port},
};

std::optional<ngx_table_elt_t> get_request_header(const ngx_list_t &headers,
                                                  std::string_view header_name,
                                                  ngx_uint_t hash) {
  dns::ngnix_header_iterable it{headers};
  auto maybe_header =
      std::find_if(it.begin(), it.end(), [header_name, hash](auto &&header) {
        return header.hash == hash && dns::key_equals_ci(header, header_name);
      });
  if (maybe_header == it.end()) {
    return std::nullopt;
  }

  return {*maybe_header};
}

using header_index_t =
    std::unordered_map<std::string_view /*lc*/, ngx_table_elt_next_t>;

void header_index_insert(header_index_t &index, const ngx_table_elt_t &header) {
  auto lc_key = dns::lc_key(header);
  auto it = index.find(lc_key);
  if (it == index.end()) {
    index.emplace(lc_key, header);
  } else {
    auto *cur = &it->second;
    while (cur->next) {
      cur = cur->next.get();
    }
    cur->next = std::make_unique<ngx_table_elt_next_t>(header);
  }
}

header_index_t index_headers(const ngx_list_t &headers) {
  header_index_t index;
  dns::ngnix_header_iterable it{headers};
  for (const ngx_table_elt_t &header : it) {
    switch (header.hash) {
#define CASE(header_lc_name)                     \
  case dns::ngx_hash_ce(header_lc_name):         \
    if (dns::lc_key(header) == header_lc_name) { \
      header_index_insert(index, header);        \
    }                                            \
    continue;
      CASE("x-forwarded-for"sv)
      CASE("x-real-ip"sv)
      CASE("true-client-ip"sv)
      CASE("x-client-ip"sv)
      CASE("x-forwarded"sv)
      CASE("forwarded-for"sv)
      CASE("x-cluster-client-ip"sv)
      CASE("fastly-client-ip"sv)
      CASE("cf-connecting-ip"sv)
      CASE("cf-connecting-ipv6"sv)
      default:
        continue;
    }
  }

  return index;
}

using extract_sv_t = extract_res (*)(std::string_view value_sv, ipaddr &);

template <extract_sv_t f>
extract_res parse_multiple(const ngx_table_elt_next_t &value, ipaddr &out) {
  ipaddr first_private{};
  for (const ngx_table_elt_next_t *h = &value; h; h = h->next.get()) {
    ipaddr out_cur_round;
    std::string_view header_v{to_string_view(h->value)};
    extract_res res = f(header_v, out_cur_round);
    if (res == extract_res::success_public()) {
      out = out_cur_round;
      return res;
    } else if (first_private.empty() && res == extract_res::success_private()) {
      first_private = out_cur_round;
    }
  }

  if (!first_private.empty()) {
    out = first_private;
    return extract_res::success_private();
  }

  return extract_res::failure();
}

extract_res parse_multiple_maybe_port(const ngx_table_elt_next_t &value,
                                      ipaddr &out) {
  return parse_multiple<parse_multiple_maybe_port_sv>(value, out);
}

extract_res parse_multiple_maybe_port_sv(std::string_view value_sv,
                                         ipaddr &out) {
  const char *value = value_sv.data();
  const char *end = value + value_sv.length();
  ipaddr first_private{};
  do {
    for (; value < end && *value == ' '; value++) {
    }
    const char *comma =
        reinterpret_cast<const char *>(std::memchr(value, ',', end - value));
    const char *end_cur = comma ? comma : end;
    std::string_view cur_str{value, static_cast<std::size_t>(end_cur - value)};
    std::optional<ipaddr> maybe_cur = parse_ip_address_maybe_port_pair(cur_str);
    if (maybe_cur) {
      if (!maybe_cur->is_private()) {
        out = *maybe_cur;
        return extract_res::success_public();
      }
      if (first_private.empty()) {
        first_private = *maybe_cur;
      }
    }
    value = (comma && comma + 1 < end) ? (comma + 1) : NULL;
  } while (value);

  if (!first_private.empty()) {
    out = first_private;
    return extract_res::success_private();
  }
  return extract_res::failure();
}

extract_res parse_forwarded(const ngx_table_elt_next_t &value, ipaddr &out) {
  return parse_multiple<parse_forwarded_sv>(value, out);
}

extract_res parse_forwarded_sv(std::string_view value_sv, ipaddr &out) {
  ipaddr first_private{};
  enum {
    between,
    key,
    before_value,
    value_token,
    value_quoted,
  } state = between;
  const char *r = value_sv.data();
  const char *end = r + value_sv.size();
  const char *start = r;  // meaningless assignment to satisfy static analysis
  bool consider_value = false;

  // https://datatracker.ietf.org/doc/html/rfc7239#section-4
  // we parse with some leniency
  while (r < end) {
    switch (state) {  // NOLINT
      case between:
        if (*r == ' ' || *r == ';' || *r == ',') {
          break;
        }
        start = r;
        state = key;
        break;
      case key:
        if (*r == '=') {
          consider_value = (r - start == 3) &&
                           (start[0] == 'f' || start[0] == 'F') &&
                           (start[1] == 'o' || start[1] == 'O') &&
                           (start[2] == 'r' || start[2] == 'R');
          state = before_value;
        }
        break;
      case before_value:
        if (*r == '"') {
          start = r + 1;
          state = value_quoted;
        } else if (*r == ' ' || *r == ';' || *r == ',') {
          // empty value; we disconsider it
          state = between;
        } else {
          start = r;
          state = value_token;
        }
        break;
      case value_token: {
        const char *token_end;
        if (*r == ' ' || *r == ';' || *r == ',') {
          token_end = r;
        } else if (r + 1 == end) {
          token_end = end;
        } else {
          break;
        }
        if (consider_value) {
          std::string_view cur_str{start,
                                   static_cast<std::size_t>(token_end - start)};
          auto maybe_cur = parse_ip_address_maybe_port_pair(cur_str);
          if (maybe_cur) {
            if (!maybe_cur->is_private()) {
              out = *maybe_cur;
              return extract_res::success_public();
            }
            if (first_private.empty() && maybe_cur->is_private()) {
              first_private = *maybe_cur;
            }
          }
        }
        state = between;
        break;
      }
      case value_quoted:
        if (*r == '"') {
          if (consider_value) {
            // ip addresses can't contain quotes, so we don't try to
            // unescape them
            std::string_view cur_str{start,
                                     static_cast<std::size_t>(r - start)};
            std::optional<ipaddr> maybe_cur =
                parse_ip_address_maybe_port_pair(cur_str);
            if (maybe_cur) {
              if (!maybe_cur->is_private()) {
                out = *maybe_cur;
                return extract_res::success_public();
              }
              if (first_private.empty() && maybe_cur->is_private()) {
                first_private = *maybe_cur;
              }
            }
          }
          state = between;
        } else if (*r == '\\') {
          r++;
        }
        break;
    }
    r++;
  }

  if (!first_private.empty()) {
    out = first_private;
    return extract_res::success_private();
  }
  return extract_res::failure();
}

std::optional<ipaddr> parse_ip_address_maybe_port_pair(
    std::string_view addr_sv) {
  if (addr_sv.empty()) {
    return std::nullopt;
  }
  if (addr_sv[0] == '[') {  // ipv6
    std::size_t pos_close = addr_sv.find(']');
    if (pos_close == std::string_view::npos) {
      return std::nullopt;
    }
    if (!pos_close) {
      return std::nullopt;
    }
    std::string_view between_brackets = addr_sv.substr(1, pos_close);
    return ipaddr::from_string(between_brackets, AF_INET6);
  }

  std::size_t first_colon = addr_sv.find(':');
  if (first_colon != std::string_view::npos &&
      addr_sv.rfind(':') == first_colon) {
    std::string_view before_colon = addr_sv.substr(0, first_colon);
    return ipaddr::from_string(before_colon, AF_INET);
  }

  return ipaddr::from_string(addr_sv);
}
}  // namespace

namespace datadog::nginx::security {

ClientIp::ClientIp(
    std::optional<ClientIp::hashed_string_view> configured_header,
    const ngx_http_request_t &request)
    : configured_header_{configured_header}, request_{request} {}

std::optional<std::string> ClientIp::resolve() const {
  if (configured_header_) {
    std::optional<ngx_table_elt_t> maybe_header =
        get_request_header(request_.headers_in.headers, configured_header_->sv,
                           configured_header_->hash);

    if (!maybe_header) {
      return std::nullopt;
    }

    ipaddr out;
    extract_res res = parse_forwarded(*maybe_header, out);
    if (res.success) {
      return out.to_string();
    }

    res = parse_multiple_maybe_port(*maybe_header, out);
    if (res.success) {
      return out.to_string();
    }

    return std::nullopt;
  }

  // path without custom defined header starts here
  header_index_t header_index = index_headers(request_.headers_in.headers);
  ipaddr cur_private{};
  for (std::size_t i = 0; i < priority_header_arr.size(); i++) {
    const header_proc_def &def = priority_header_arr[i];
    auto maybe_elem = header_index.find(def.lc_key_);
    if (maybe_elem == header_index.end()) {
      continue;
    }

    const ngx_table_elt_next_t &header = maybe_elem->second;
    ipaddr out;
    extract_res res = def.parse_func_(header, out);
    if (res.success) {
      if (!res.is_private) {
        return out.to_string();
      }
      if (cur_private.empty()) {
        cur_private = out;
      }
    }
  }

  // No public address found yet
  // Try remote_addr. If it's public we'll use it
  ipaddr remote_addr{};
  struct sockaddr *sockaddr = request_.connection->sockaddr;
  if (sockaddr->sa_family == AF_INET) {
    remote_addr.af = AF_INET;
    remote_addr.u.v4 = reinterpret_cast<sockaddr_in *>(sockaddr)->sin_addr;
  } else if (sockaddr->sa_family == AF_INET6) {
    remote_addr.af = AF_INET6;
    remote_addr.u.v6 = reinterpret_cast<sockaddr_in6 *>(sockaddr)->sin6_addr;
  }

  if (!remote_addr.empty()) {
    if (remote_addr.is_private()) {
      if (cur_private.empty()) {
        return {remote_addr.to_string()};
      } else {
        return {cur_private.to_string()};
      }
    }
  }

  // no remote address
  if (!cur_private.empty()) {
    return {cur_private.to_string()};
  }

  return std::nullopt;
}
}  // namespace datadog::nginx::security
