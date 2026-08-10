#include <catch2/catch_test_macros.hpp>

#include <string>

#include "nginx_package_abi.h"

namespace package_abi = datadog::nginx::package_abi;

TEST_CASE("recognize an Ubuntu nginx build") {
  CHECK(package_abi::is_debian_or_ubuntu_build(
      "nginx version: nginx/1.28.3 (Ubuntu)"));
  CHECK(package_abi::is_debian_or_ubuntu_build(
      "configure arguments: --with-compat --build=Ubuntu"));
}

TEST_CASE("recognize a Debian nginx build") {
  constexpr std::string_view version_info = R"(
nginx version: nginx/1.30.1
configure arguments:
--with-cc-opt='-g -O2 -ffile-prefix-map=/build/reproducible-path/nginx-1.30.1=.'
--prefix=/usr/share/nginx
--modules-path=/usr/lib/nginx/modules
--http-client-body-temp-path=/var/lib/nginx/body
)";

  CHECK(package_abi::is_debian_or_ubuntu_build(version_info));
}

TEST_CASE("do not mistake a Debian compiler for a Debian nginx package") {
  constexpr std::string_view version_info = R"(
nginx version: nginx/1.28.3
built by gcc 14.2.0 (Debian 14.2.0-19)
configure arguments: --prefix=/etc/nginx --modules-path=/usr/lib/nginx/modules
)";

  CHECK_FALSE(package_abi::is_debian_or_ubuntu_build(version_info));
}

TEST_CASE("require the complete Debian package fingerprint") {
  constexpr std::string_view build_path =
      "-ffile-prefix-map=/build/reproducible-path/nginx-1.30.1=.";

  CHECK_FALSE(package_abi::is_debian_or_ubuntu_build(build_path));
  CHECK_FALSE(package_abi::is_debian_or_ubuntu_build(
      std::string{build_path} + " --prefix=/usr/share/nginx"));
  CHECK_FALSE(package_abi::is_debian_or_ubuntu_build(
      std::string{build_path} + " --prefix=/usr/share/nginx "
                               "--modules-path=/usr/lib/nginx/modules"));
}

TEST_CASE("do not identify an upstream nginx build as Debian or Ubuntu") {
  constexpr std::string_view version_info = R"(
nginx version: nginx/1.28.3
configure arguments: --prefix=/etc/nginx
--modules-path=/usr/lib/nginx/modules
--http-client-body-temp-path=/var/cache/nginx/client_temp
)";

  CHECK_FALSE(package_abi::is_debian_or_ubuntu_build(version_info));
}
