#pragma once

#include <string_view>

namespace datadog::nginx::package_abi {

bool is_debian_or_ubuntu_build(std::string_view version_info) noexcept;
bool running_nginx_is_debian_or_ubuntu() noexcept;
bool module_was_built_for_debian_or_ubuntu() noexcept;

}  // namespace datadog::nginx::package_abi
