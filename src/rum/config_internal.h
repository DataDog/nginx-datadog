#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace datadog::nginx::rum::internal {

inline constexpr int default_rum_config_version = 5;

// libdd-library-config "language" identifier used to select RUM rules in
// application_monitoring.yaml.
inline constexpr std::string_view rum_language = "nginx";

std::optional<bool> parse_bool(std::string_view value);

using rum_config_map =
    std::unordered_map<std::string, std::vector<std::string>>;

std::string make_rum_json_config(int config_version,
                                 const rum_config_map& config);

std::optional<int> parse_rum_version(std::string_view config_version);

}  // namespace datadog::nginx::rum::internal
