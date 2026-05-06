#include <catch2/catch_test_macros.hpp>
#include <rapidjson/document.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rum/config_internal.h"

namespace rum = datadog::nginx::rum::internal;

namespace {

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

  auto json = rum::make_rum_json_config(rum::default_rum_config_version, config);
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

  auto json = rum::make_rum_json_config(rum::default_rum_config_version, config);
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

  auto json = rum::make_rum_json_config(rum::default_rum_config_version, config);
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
      auto json = rum::make_rum_json_config(rum::default_rum_config_version, config);
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
      auto json = rum::make_rum_json_config(rum::default_rum_config_version, config);
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

  auto json = rum::make_rum_json_config(rum::default_rum_config_version, config);
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

  auto json = rum::make_rum_json_config(rum::default_rum_config_version, config);
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

  auto json = rum::make_rum_json_config(rum::default_rum_config_version, config);
  auto doc = parse_json(json);

  // Invalid double values are passed as strings instead of crashing.
  REQUIRE(doc["rum"].HasMember("sessionSampleRate"));
  CHECK(doc["rum"]["sessionSampleRate"].IsString());
  CHECK(std::string(doc["rum"]["sessionSampleRate"].GetString()) ==
        "not-a-number");
}

TEST_CASE("make_rum_json_config with empty config", "[rum][config]") {
  std::unordered_map<std::string, std::vector<std::string>> config;

  auto json = rum::make_rum_json_config(rum::default_rum_config_version, config);
  auto doc = parse_json(json);

  CHECK(doc["majorVersion"].GetInt() == 5);
  REQUIRE(doc.HasMember("rum"));
  CHECK(doc["rum"].ObjectEmpty());
}

