#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace datadog::nginx::rum::internal {

// Parse a string as a boolean. Returns true for "true", "1", "yes", "on"
// and false for "false", "0", "no", "off" (case-insensitive).
// Returns nullopt for unrecognized values.
std::optional<bool> parse_bool(std::string_view value);

struct env_mapping {
  std::string_view env_name;
  std::string_view config_key;
};

extern const env_mapping rum_env_mappings[];
extern const std::size_t rum_env_mappings_size;

std::unordered_map<std::string, std::vector<std::string>>
get_rum_config_from_env();

std::optional<bool> get_rum_enabled_from_env();

std::string make_rum_json_config(
    int config_version,
    const std::unordered_map<std::string, std::vector<std::string>>& config);

std::optional<int> parse_rum_version(std::string_view config_version);

}  // namespace datadog::nginx::rum::internal
