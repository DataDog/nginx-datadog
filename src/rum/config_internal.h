#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace datadog::nginx::rum::internal {

inline constexpr int default_rum_config_version = 5;

// Parse a string as a boolean. Returns true for "true", "1", "yes", "on"
// and false for "false", "0", "no", "off" (case-insensitive).
// Returns nullopt for unrecognized values.
std::optional<bool> parse_bool(std::string_view value);

struct env_mapping {
  std::string_view env_name;
  std::string_view config_key;
};

inline constexpr auto rum_env_mappings = std::to_array<env_mapping>({
    {"DD_RUM_APPLICATION_ID", "applicationId"},
    {"DD_RUM_CLIENT_TOKEN", "clientToken"},
    {"DD_RUM_SITE", "site"},
    {"DD_RUM_SERVICE", "service"},
    {"DD_RUM_ENVIRONMENT", "env"},
    {"DD_RUM_MAJOR_VERSION", "version"},
    {"DD_RUM_SESSION_SAMPLE_RATE", "sessionSampleRate"},
    {"DD_RUM_SESSION_REPLAY_SAMPLE_RATE", "sessionReplaySampleRate"},
    {"DD_RUM_TRACK_RESOURCES", "trackResources"},
    {"DD_RUM_TRACK_LONG_TASKS", "trackLongTasks"},
    {"DD_RUM_TRACK_USER_INTERACTIONS", "trackUserInteractions"},
    {"DD_RUM_REMOTE_CONFIGURATION_ID", "remoteConfigurationId"},
});

using rum_config_map = std::unordered_map<std::string, std::vector<std::string>>;

rum_config_map get_rum_config_from_env();

std::optional<bool> get_rum_enabled_from_env();

std::string make_rum_json_config(int config_version,
                                 const rum_config_map& config);

std::optional<int> parse_rum_version(std::string_view config_version);

}  // namespace datadog::nginx::rum::internal
