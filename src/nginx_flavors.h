#include <cstdint>
#include <string_view>

namespace datadog {
namespace nginx {

enum class flavor : uint8_t {
  vanilla,
  openresty,
  ingress_nginx,
};

inline constexpr flavor from_str(std::string_view in) {
  if (in == "nginx") {
    return flavor::vanilla;
  } else if (in == "openresty") {
    return flavor::vanilla;
  } else if (in == "ingress-nginx") {
    return flavor::ingress_nginx;
  }

  static_assert(true, "unknown NGINX flavor");
  std::abort();
}

inline constexpr flavor kNginx_flavor = from_str(DD_NGINX_FLAVOR);

}  // namespace nginx
}  // namespace datadog
