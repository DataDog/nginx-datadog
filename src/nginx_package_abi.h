#pragma once

#include <string_view>

namespace datadog::nginx::package_abi {

bool is_debian_or_ubuntu_build(std::string_view version_info) noexcept;
bool is_running_nginx_is_debian_or_ubuntu() noexcept;
bool is_module_built_for_debian_or_ubuntu() noexcept;

}  // namespace datadog::nginx::package_abi
