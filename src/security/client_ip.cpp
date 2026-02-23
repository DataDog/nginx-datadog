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
namespace dnsec = datadog::nginx::security;
using datadog::nginx::to_string_view;

namespace {

struct IpAddr {
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
    if (inet_ntop(af, reinterpret_cast<const char*>(&this->u), buf,
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

  static std::optional<IpAddr> from_string(std::string_view addr_sv,
                                           int af_hint = AF_UNSPEC);
};

std::optional<IpAddr> IpAddr::from_string(std::string_view addr_sv,
                                          int af_hint) {
  IpAddr out;
  std::string input_nulterminated{addr_sv};
  auto* addr_nult = input_nulterminated.c_str();

  if (af_hint == AF_INET || af_hint == AF_UNSPEC) {
    int ret = inet_pton(AF_INET, addr_nult, &out.u.v4);
    if (ret == 1) {
      out.af = AF_INET;
      return {out};
    }

    if (af_hint == AF_INET) {
      // might still be an ipv6-mapped ipv4 address, but we interpret af_hint
      // as indicating the formal type of the address (see usages)
      return std::nullopt;
    }
  }

  int ret = inet_pton(AF_INET6, addr_nult, &out.u.v6);
  if (ret != 1) {
    // neither valid ipv4 nor ipv6
    return std::nullopt;
  }

  // if we got here, we have a valid formal ipv6 address

  uint8_t* s6addr = out.u.v6.s6_addr;
  static constexpr uint8_t ip4_mapped_prefix[12] = {0, 0, 0, 0, 0,    0,
                                                    0, 0, 0, 0, 0xFF, 0xFF};
  if (std::memcmp(s6addr, ip4_mapped_prefix, sizeof(ip4_mapped_prefix)) == 0) {
    // IPv4 mapped
    std::memcpy(&out.u.v4.s_addr, s6addr + sizeof(ip4_mapped_prefix), 4);
    out.af = AF_INET;
  } else {
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

bool IpAddr::is_private_v4() const {
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
      {
          .base = {ct_htonl(0x64400000U)},  // 100.64.0.0
          .mask = {ct_htonl(0xFFC00000U)},  // 255.192.0.0
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

bool IpAddr::is_private_v6() const {
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

struct ExtractResult {
  bool success;
  bool is_private;

  static inline ExtractResult success_public() { return {true, false}; }
  static inline ExtractResult success_private() { return {true, true}; }
  static inline ExtractResult failure() { return {false}; }

  bool operator==(const ExtractResult& other) const {
    return (success && other.success && is_private == other.is_private) ||
           (!success && !other.success);
  }
};

struct NgxTableEltNextT : ngx_table_elt_t {
  NgxTableEltNextT(const ngx_table_elt_t& header) : ngx_table_elt_t{header} {}

  std::unique_ptr<NgxTableEltNextT> next{};
};

struct HeaderProcessorDefinition {
 public:
  using ExtractFunc = ExtractResult (*)(const NgxTableEltNextT& value,
                                        IpAddr& out);

  constexpr HeaderProcessorDefinition(std::string_view key,
                                      ExtractFunc parse_func)
      : lc_key{key},
        lc_key_hash{dnsec::ngx_hash_ce(key)},
        parse_func{parse_func} {}

  std::string_view lc_key;
  ngx_uint_t lc_key_hash;
  ExtractFunc parse_func;
};
// clang-format off
ExtractResult parse_multiple_maybe_port(const NgxTableEltNextT& value, IpAddr &out);
ExtractResult parse_multiple_maybe_port_sv(std::string_view sv, IpAddr &out);
ExtractResult parse_forwarded(const NgxTableEltNextT& value, IpAddr &out);
ExtractResult parse_forwarded_sv(std::string_view sv, IpAddr &out);
std::optional<IpAddr> parse_ip_address_maybe_port_pair(std::string_view sv);
// clang-format on

static constexpr auto kPriorityHeaderArr =
    std::array<HeaderProcessorDefinition, 11>{
        HeaderProcessorDefinition{"x-forwarded-for"sv,
                                  parse_multiple_maybe_port},
        {"x-real-ip"sv, parse_multiple_maybe_port},
        {"true-client-ip"sv, parse_multiple_maybe_port},
        {"x-client-ip"sv, parse_multiple_maybe_port},
        {"x-forwarded"sv, parse_forwarded},
        {"forwarded"sv, parse_forwarded},
        {"forwarded-for"sv, parse_multiple_maybe_port},
        {"x-cluster-client-ip"sv, parse_multiple_maybe_port},
        {"fastly-client-ip"sv, parse_multiple_maybe_port},
        {"cf-connecting-ip"sv, parse_multiple_maybe_port},
        {"cf-connecting-ipv6"sv, parse_multiple_maybe_port},
    };

std::optional<ngx_table_elt_t> get_request_header(const ngx_list_t& headers,
                                                  std::string_view header_name,
                                                  ngx_uint_t hash) {
  dnsec::NgnixHeaderIterable it{headers};
  auto maybe_header =
      std::find_if(it.begin(), it.end(), [header_name, hash](auto&& header) {
        return header.hash == hash &&
               dnsec::req_key_equals_ci(header, header_name);
      });
  if (maybe_header == it.end()) {
    return std::nullopt;
  }

  return {*maybe_header};
}

using HeaderIndex =
    std::unordered_map<std::string_view /*lc*/, NgxTableEltNextT>;

void header_index_insert(HeaderIndex& index, const ngx_table_elt_t& header) {
  auto lc_key = dnsec::lc_key(header);
  auto it = index.find(lc_key);
  if (it == index.end()) {
    index.emplace(lc_key, header);
  } else {
    auto* cur = &it->second;
    while (cur->next) {
      cur = cur->next.get();
    }
    cur->next = std::make_unique<NgxTableEltNextT>(header);
  }
}

HeaderIndex index_headers(const ngx_list_t& headers) {
  HeaderIndex index;
  dnsec::NgnixHeaderIterable it{headers};
  for (const ngx_table_elt_t& header : it) {
    switch (header.hash) {
#define CASE(header_lc_name)                       \
  case dnsec::ngx_hash_ce(header_lc_name):         \
    if (dnsec::lc_key(header) == header_lc_name) { \
      header_index_insert(index, header);          \
    }                                              \
    continue;
      CASE("x-forwarded-for"sv)
      CASE("x-real-ip"sv)
      CASE("true-client-ip"sv)
      CASE("x-client-ip"sv)
      CASE("x-forwarded"sv)
      CASE("forwarded"sv)
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

using ExtractStringViewFunc = ExtractResult (*)(std::string_view value_sv,
                                                IpAddr&);

template <ExtractStringViewFunc f>
ExtractResult parse_multiple(const NgxTableEltNextT& value, IpAddr& out) {
  IpAddr first_private{};
  for (const NgxTableEltNextT* h = &value; h; h = h->next.get()) {
    IpAddr out_cur_round;
    std::string_view header_v{to_string_view(h->value)};
    ExtractResult res = f(header_v, out_cur_round);
    if (res == ExtractResult::success_public()) {
      out = out_cur_round;
      return res;
    } else if (first_private.empty() &&
               res == ExtractResult::success_private()) {
      first_private = out_cur_round;
    }
  }

  if (!first_private.empty()) {
    out = first_private;
    return ExtractResult::success_private();
  }

  return ExtractResult::failure();
}

ExtractResult parse_multiple_maybe_port(const NgxTableEltNextT& value,
                                        IpAddr& out) {
  return parse_multiple<parse_multiple_maybe_port_sv>(value, out);
}

ExtractResult parse_multiple_maybe_port_sv(std::string_view value_sv,
                                           IpAddr& out) {
  const char* value = value_sv.data();
  const char* end = value + value_sv.length();
  IpAddr first_private{};
  do {
    for (; value < end && *value == ' '; value++) {
    }
    const char* comma =
        reinterpret_cast<const char*>(std::memchr(value, ',', end - value));
    const char* end_cur = comma ? comma : end;
    std::string_view cur_str{value, static_cast<std::size_t>(end_cur - value)};
    std::optional<IpAddr> maybe_cur = parse_ip_address_maybe_port_pair(cur_str);
    if (maybe_cur) {
      if (!maybe_cur->is_private()) {
        out = *maybe_cur;
        return ExtractResult::success_public();
      }
      if (first_private.empty()) {
        first_private = *maybe_cur;
      }
    }
    value = (comma && comma + 1 < end) ? (comma + 1) : nullptr;
  } while (value);

  if (!first_private.empty()) {
    out = first_private;
    return ExtractResult::success_private();
  }
  return ExtractResult::failure();
}

ExtractResult parse_forwarded(const NgxTableEltNextT& value, IpAddr& out) {
  return parse_multiple<parse_forwarded_sv>(value, out);
}

ExtractResult parse_forwarded_sv(std::string_view value_sv, IpAddr& out) {
  IpAddr first_private{};
  enum {
    BETWEEN,
    KEY,
    BEFORE_VALUE,
    VALUE_TOKEN,
    VALUE_QUOTED,
  } state = BETWEEN;
  const char* r = value_sv.data();
  const char* end = r + value_sv.size();
  const char* start = r;  // meaningless assignment to satisfy static analysis
  bool consider_value = false;

  // https://datatracker.ietf.org/doc/html/rfc7239#section-4
  // we parse with some leniency
  while (r < end) {
    switch (state) {  // NOLINT
      case BETWEEN:
        if (*r == ' ' || *r == ';' || *r == ',') {
          break;
        }
        start = r;
        state = KEY;
        break;
      case KEY:
        if (*r == '=') {
          consider_value = (r - start == 3) &&
                           (start[0] == 'f' || start[0] == 'F') &&
                           (start[1] == 'o' || start[1] == 'O') &&
                           (start[2] == 'r' || start[2] == 'R');
          state = BEFORE_VALUE;
        }
        break;
      case BEFORE_VALUE:
        if (*r == '"') {
          start = r + 1;
          state = VALUE_QUOTED;
        } else if (*r == ' ' || *r == ';' || *r == ',') {
          // empty value; we disconsider it
          state = BETWEEN;
        } else {
          start = r;
          state = VALUE_TOKEN;
        }
        break;
      case VALUE_TOKEN: {
        const char* token_end;
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
              return ExtractResult::success_public();
            }
            if (first_private.empty() && maybe_cur->is_private()) {
              first_private = *maybe_cur;
            }
          }
        }
        state = BETWEEN;
        break;
      }
      case VALUE_QUOTED:
        if (*r == '"') {
          if (consider_value) {
            // ip addresses can't contain quotes, so we don't try to
            // unescape them
            std::string_view cur_str{start,
                                     static_cast<std::size_t>(r - start)};
            std::optional<IpAddr> maybe_cur =
                parse_ip_address_maybe_port_pair(cur_str);
            if (maybe_cur) {
              if (!maybe_cur->is_private()) {
                out = *maybe_cur;
                return ExtractResult::success_public();
              }
              if (first_private.empty() && maybe_cur->is_private()) {
                first_private = *maybe_cur;
              }
            }
          }
          state = BETWEEN;
        } else if (*r == '\\') {
          r++;
        }
        break;
    }
    r++;
  }

  if (!first_private.empty()) {
    out = first_private;
    return ExtractResult::success_private();
  }
  return ExtractResult::failure();
}

std::optional<IpAddr> parse_ip_address_maybe_port_pair(
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
    return IpAddr::from_string(between_brackets, AF_INET6);
  }

  std::size_t first_colon = addr_sv.find(':');
  if (first_colon != std::string_view::npos &&
      addr_sv.rfind(':') == first_colon) {
    std::string_view before_colon = addr_sv.substr(0, first_colon);
    return IpAddr::from_string(before_colon, AF_INET);
  }

  return IpAddr::from_string(addr_sv);
}
}  // namespace

namespace datadog::nginx::security {

ClientIp::ClientIp(std::optional<HashedStringView> configured_header,
                   const ngx_http_request_t& request)
    : configured_header_{configured_header}, request_{request} {}

std::optional<std::string> ClientIp::resolve() const {
  if (configured_header_) {
    std::optional<ngx_table_elt_t> maybe_header =
        get_request_header(request_.headers_in.headers, configured_header_->str,
                           configured_header_->hash);

    if (!maybe_header) {
      return std::nullopt;
    }

    IpAddr out;
    ExtractResult res = parse_forwarded(*maybe_header, out);
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
  HeaderIndex header_index = index_headers(request_.headers_in.headers);
  IpAddr cur_private{};
  for (std::size_t i = 0; i < kPriorityHeaderArr.size(); i++) {
    const HeaderProcessorDefinition& def = kPriorityHeaderArr[i];
    auto maybe_elem = header_index.find(def.lc_key);
    if (maybe_elem == header_index.end()) {
      continue;
    }

    const NgxTableEltNextT& header = maybe_elem->second;
    IpAddr out;
    ExtractResult res = def.parse_func(header, out);
    if (res.success) {
      if (!res.is_private) {
        return out.to_string();
      }
      if (cur_private.empty()) {
        cur_private = out;
      }
    }
  }

  // No public address found yet. Try remote_addr.
  IpAddr remote_addr{};
  struct sockaddr* sockaddr = request_.connection->sockaddr;
  if (sockaddr->sa_family == AF_INET) {
    remote_addr.af = AF_INET;
    remote_addr.u.v4 = reinterpret_cast<sockaddr_in*>(sockaddr)->sin_addr;
  } else if (sockaddr->sa_family == AF_INET6) {
    remote_addr.af = AF_INET6;
    remote_addr.u.v6 = reinterpret_cast<sockaddr_in6*>(sockaddr)->sin6_addr;
  }

  if (!remote_addr.empty()) {
    if (!remote_addr.is_private()) {
      return remote_addr.to_string();
    }
    if (cur_private.empty()) {
      return remote_addr.to_string();
    }  // else cur_private is preferred below
  }

  // no remote address
  if (!cur_private.empty()) {
    return {cur_private.to_string()};
  }

  return std::nullopt;
}
}  // namespace datadog::nginx::security
