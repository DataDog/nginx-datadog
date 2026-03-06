#include <rapidjson/document.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rum/config_internal.h"

namespace rum = datadog::nginx::rum::internal;

namespace {

// RAII helper that sets an environment variable on construction
// and restores the original state on destruction.
struct ScopedEnv {
  std::string name;
  std::optional<std::string> old_value;

  ScopedEnv(const char* env_name, const char* value) : name(env_name) {
    const char* prev = std::getenv(env_name);
    if (prev) {
      old_value = prev;
    }
    setenv(env_name, value, 1);
  }

  ~ScopedEnv() {
    if (old_value) {
      setenv(name.c_str(), old_value->c_str(), 1);
    } else {
      unsetenv(name.c_str());
    }
  }

  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;
};

// RAII helper that unsets an environment variable on construction
// and restores the original state on destruction.
struct ScopedUnsetEnv {
  std::string name;
  std::optional<std::string> old_value;

  explicit ScopedUnsetEnv(const char* env_name) : name(env_name) {
    const char* prev = std::getenv(env_name);
    if (prev) {
      old_value = prev;
    }
    unsetenv(env_name);
  }

  ~ScopedUnsetEnv() {
    if (old_value) {
      setenv(name.c_str(), old_value->c_str(), 1);
    } else {
      unsetenv(name.c_str());
    }
  }

  ScopedUnsetEnv(const ScopedUnsetEnv&) = delete;
  ScopedUnsetEnv& operator=(const ScopedUnsetEnv&) = delete;
};

// Helper: parse JSON string and return a RapidJSON Document.
rapidjson::Document parse_json(const std::string& json) {
  rapidjson::Document doc;
  doc.Parse(json.c_str());
  REQUIRE(!doc.HasParseError());
  return doc;
}

}  // namespace

// ---------------------------------------------------------------------------
// parse_rum_version
// ---------------------------------------------------------------------------

TEST_CASE("parse_rum_version valid inputs", "[rum][config]") {
  CHECK(rum::parse_rum_version("v5") == 5);
  CHECK(rum::parse_rum_version("v1") == 1);
  CHECK(rum::parse_rum_version("v0") == 0);
  CHECK(rum::parse_rum_version("v100") == 100);
  // Trailing non-digit characters: from_chars stops at '.', returns 5
  CHECK(rum::parse_rum_version("v5.0") == 5);
}

TEST_CASE("parse_rum_version invalid inputs", "[rum][config]") {
  CHECK(rum::parse_rum_version("") == std::nullopt);
  CHECK(rum::parse_rum_version("v") == std::nullopt);
  CHECK(rum::parse_rum_version("5") == std::nullopt);   // no 'v' prefix
  CHECK(rum::parse_rum_version("V5") == std::nullopt);  // uppercase
  CHECK(rum::parse_rum_version("va") == std::nullopt);  // non-numeric
  CHECK(rum::parse_rum_version("abc") == std::nullopt);
}

// ---------------------------------------------------------------------------
// parse_bool
// ---------------------------------------------------------------------------

TEST_CASE("parse_bool truthy values", "[rum][config]") {
  for (const char* val : {"true", "TRUE", "True", "1", "yes", "YES", "on",
                          "ON", "On"}) {
    SECTION(val) {
      auto result = rum::parse_bool(val);
      REQUIRE(result.has_value());
      CHECK(*result == true);
    }
  }
}

TEST_CASE("parse_bool falsy values", "[rum][config]") {
  for (const char* val : {"false", "FALSE", "False", "0", "no", "NO", "off",
                          "OFF", "Off"}) {
    SECTION(val) {
      auto result = rum::parse_bool(val);
      REQUIRE(result.has_value());
      CHECK(*result == false);
    }
  }
}

TEST_CASE("parse_bool unrecognized values return nullopt", "[rum][config]") {
  for (const char* val : {"maybe", "2", "enabled", ""}) {
    SECTION(std::string("'") + val + "'") {
      CHECK(rum::parse_bool(val) == std::nullopt);
    }
  }
}

// ---------------------------------------------------------------------------
// make_rum_json_config
// ---------------------------------------------------------------------------

TEST_CASE("make_rum_json_config with string fields", "[rum][config]") {
  std::unordered_map<std::string, std::vector<std::string>> config = {
      {"applicationId", {"app-123"}},
      {"clientToken", {"tok-456"}},
  };

  auto json = rum::make_rum_json_config(5, config);
  auto doc = parse_json(json);

  CHECK(doc["majorVersion"].GetInt() == 5);
  REQUIRE(doc.HasMember("rum"));
  CHECK(std::string(doc["rum"]["applicationId"].GetString()) == "app-123");
  CHECK(std::string(doc["rum"]["clientToken"].GetString()) == "tok-456");
}

TEST_CASE("make_rum_json_config with double fields", "[rum][config]") {
  std::unordered_map<std::string, std::vector<std::string>> config = {
      {"sessionSampleRate", {"75.5"}},
      {"sessionReplaySampleRate", {"50"}},
  };

  auto json = rum::make_rum_json_config(5, config);
  auto doc = parse_json(json);

  CHECK(doc["rum"]["sessionSampleRate"].GetDouble() == 75.5);
  CHECK(doc["rum"]["sessionReplaySampleRate"].GetDouble() == 50.0);
}

TEST_CASE("make_rum_json_config with bool fields", "[rum][config]") {
  std::unordered_map<std::string, std::vector<std::string>> config = {
      {"trackResources", {"true"}},
      {"trackLongTasks", {"false"}},
      {"trackUserInteractions", {"true"}},
  };

  auto json = rum::make_rum_json_config(5, config);
  auto doc = parse_json(json);

  CHECK(doc["rum"]["trackResources"].GetBool() == true);
  CHECK(doc["rum"]["trackLongTasks"].GetBool() == false);
  CHECK(doc["rum"]["trackUserInteractions"].GetBool() == true);
}

TEST_CASE("make_rum_json_config bool fields accept truthy variants",
          "[rum][config]") {
  for (const char* truthy : {"true", "TRUE", "True", "1", "yes", "YES",
                              "on", "ON"}) {
    SECTION(std::string("trackResources=") + truthy) {
      std::unordered_map<std::string, std::vector<std::string>> config = {
          {"trackResources", {truthy}},
      };
      auto json = rum::make_rum_json_config(5, config);
      auto doc = parse_json(json);
      CHECK(doc["rum"]["trackResources"].GetBool() == true);
    }
  }

  for (const char* falsy : {"false", "FALSE", "0", "no", "off",
                             "anything_else"}) {
    SECTION(std::string("trackResources=") + falsy) {
      std::unordered_map<std::string, std::vector<std::string>> config = {
          {"trackResources", {falsy}},
      };
      auto json = rum::make_rum_json_config(5, config);
      auto doc = parse_json(json);
      CHECK(doc["rum"]["trackResources"].GetBool() == false);
    }
  }
}

TEST_CASE("make_rum_json_config skips entries with empty values vector",
          "[rum][config]") {
  std::unordered_map<std::string, std::vector<std::string>> config = {
      {"applicationId", {"app-123"}},
      {"sessionSampleRate", {}},
      {"trackResources", {}},
      {"customField", {}},
  };

  auto json = rum::make_rum_json_config(5, config);
  auto doc = parse_json(json);

  CHECK(std::string(doc["rum"]["applicationId"].GetString()) == "app-123");
  CHECK(!doc["rum"].HasMember("sessionSampleRate"));
  CHECK(!doc["rum"].HasMember("trackResources"));
  CHECK(!doc["rum"].HasMember("customField"));
}

TEST_CASE("make_rum_json_config with multi-value array", "[rum][config]") {
  std::unordered_map<std::string, std::vector<std::string>> config = {
      {"customField", {"val1", "val2", "val3"}},
  };

  auto json = rum::make_rum_json_config(5, config);
  auto doc = parse_json(json);

  REQUIRE(doc["rum"]["customField"].IsArray());
  auto arr = doc["rum"]["customField"].GetArray();
  REQUIRE(arr.Size() == 3);
  CHECK(std::string(arr[0].GetString()) == "val1");
  CHECK(std::string(arr[1].GetString()) == "val2");
  CHECK(std::string(arr[2].GetString()) == "val3");
}

TEST_CASE("make_rum_json_config with invalid double falls back to string",
          "[rum][config]") {
  std::unordered_map<std::string, std::vector<std::string>> config = {
      {"sessionSampleRate", {"not-a-number"}},
  };

  auto json = rum::make_rum_json_config(5, config);
  auto doc = parse_json(json);

  // Invalid double values are passed as strings instead of crashing.
  REQUIRE(doc["rum"].HasMember("sessionSampleRate"));
  CHECK(doc["rum"]["sessionSampleRate"].IsString());
  CHECK(std::string(doc["rum"]["sessionSampleRate"].GetString()) ==
        "not-a-number");
}

TEST_CASE("make_rum_json_config with empty config", "[rum][config]") {
  std::unordered_map<std::string, std::vector<std::string>> config;

  auto json = rum::make_rum_json_config(5, config);
  auto doc = parse_json(json);

  CHECK(doc["majorVersion"].GetInt() == 5);
  REQUIRE(doc.HasMember("rum"));
  CHECK(doc["rum"].ObjectEmpty());
}

// ---------------------------------------------------------------------------
// get_rum_enabled_from_env
// ---------------------------------------------------------------------------

TEST_CASE("get_rum_enabled_from_env truthy values", "[rum][config]") {
  for (const char* val : {"true", "TRUE", "True", "1", "yes", "on"}) {
    SECTION(std::string("DD_RUM_ENABLED=") + val) {
      ScopedEnv env("DD_RUM_ENABLED", val);
      auto result = rum::get_rum_enabled_from_env();
      REQUIRE(result.has_value());
      CHECK(*result == true);
    }
  }
}

TEST_CASE("get_rum_enabled_from_env falsy values", "[rum][config]") {
  for (const char* val : {"false", "FALSE", "False", "0", "no", "off"}) {
    SECTION(std::string("DD_RUM_ENABLED=") + val) {
      ScopedEnv env("DD_RUM_ENABLED", val);
      auto result = rum::get_rum_enabled_from_env();
      REQUIRE(result.has_value());
      CHECK(*result == false);
    }
  }
}

TEST_CASE("get_rum_enabled_from_env unset returns nullopt", "[rum][config]") {
  ScopedUnsetEnv env("DD_RUM_ENABLED");
  CHECK(rum::get_rum_enabled_from_env() == std::nullopt);
}

TEST_CASE("get_rum_enabled_from_env empty string returns nullopt",
          "[rum][config]") {
  ScopedEnv env("DD_RUM_ENABLED", "");
  CHECK(rum::get_rum_enabled_from_env() == std::nullopt);
}

TEST_CASE("get_rum_enabled_from_env unrecognized value returns nullopt",
          "[rum][config]") {
  ScopedEnv env("DD_RUM_ENABLED", "maybe");
  CHECK(rum::get_rum_enabled_from_env() == std::nullopt);
}

// ---------------------------------------------------------------------------
// get_rum_config_from_env
// ---------------------------------------------------------------------------

TEST_CASE("get_rum_config_from_env reads set variables", "[rum][config]") {
  // Unset all RUM env vars first to get a clean slate.
  std::vector<std::unique_ptr<ScopedUnsetEnv>> unsets;
  for (std::size_t i = 0; i < rum::rum_env_mappings_size; ++i) {
    unsets.push_back(std::make_unique<ScopedUnsetEnv>(
        std::string(rum::rum_env_mappings[i].env_name).c_str()));
  }

  ScopedEnv app_id("DD_RUM_APPLICATION_ID", "my-app");
  ScopedEnv token("DD_RUM_CLIENT_TOKEN", "my-token");

  auto config = rum::get_rum_config_from_env();
  CHECK(config.size() == 2);
  REQUIRE(config.count("applicationId"));
  CHECK(config["applicationId"] == std::vector<std::string>{"my-app"});
  REQUIRE(config.count("clientToken"));
  CHECK(config["clientToken"] == std::vector<std::string>{"my-token"});
}

TEST_CASE("get_rum_config_from_env skips unset variables", "[rum][config]") {
  // Unset all RUM env vars.
  std::vector<std::unique_ptr<ScopedUnsetEnv>> unsets;
  for (std::size_t i = 0; i < rum::rum_env_mappings_size; ++i) {
    unsets.push_back(std::make_unique<ScopedUnsetEnv>(
        std::string(rum::rum_env_mappings[i].env_name).c_str()));
  }

  auto config = rum::get_rum_config_from_env();
  CHECK(config.empty());
}

TEST_CASE("get_rum_config_from_env skips empty values", "[rum][config]") {
  // Unset all RUM env vars first.
  std::vector<std::unique_ptr<ScopedUnsetEnv>> unsets;
  for (std::size_t i = 0; i < rum::rum_env_mappings_size; ++i) {
    unsets.push_back(std::make_unique<ScopedUnsetEnv>(
        std::string(rum::rum_env_mappings[i].env_name).c_str()));
  }

  ScopedEnv empty_val("DD_RUM_APPLICATION_ID", "");

  auto config = rum::get_rum_config_from_env();
  CHECK(config.empty());
}
