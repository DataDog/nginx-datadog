#include <fstream>
#include <string>

#include <rapidjson/prettywriter.h>
#include <rapidjson/schema.h>
#include <rapidjson/error/en.h>

#include "security_library.h"

namespace datadog {
namespace nginx {

namespace {

template <typename T> void json_to_object(ddwaf_object *object, T &doc)
{
    switch (doc.GetType()) {
    case rapidjson::kFalseType:
        ddwaf_object_stringl(object, "false", sizeof("false") - 1);
        break;
    case rapidjson::kTrueType:
        ddwaf_object_stringl(object, "true", sizeof("true") - 1);
        break;
    case rapidjson::kObjectType: {
        ddwaf_object_map(object);
        for (auto &kv : doc.GetObject()) {
            ddwaf_object element;
            json_to_object(&element, kv.value);

            string_view key = kv.name.GetString();
            ddwaf_object_map_addl(object, key.data(), key.length(), &element);
        }
        break;
    }
    case rapidjson::kArrayType: {
        ddwaf_object_array(object);
        for (auto &v : doc.GetArray()) {
            ddwaf_object element;
            json_to_object(&element, v);

            ddwaf_object_array_add(object, &element);
        }
        break;
    }
    case rapidjson::kStringType: {
        string_view str = doc.GetString();
        ddwaf_object_stringl(object, str.data(), str.size());
        break;
    }
    case rapidjson::kNumberType: {
        if (doc.IsInt64()) {
            ddwaf_object_signed(object, doc.GetInt64());
        } else if (doc.IsUint64()) {
            ddwaf_object_unsigned(object, doc.GetUint64());
        }
        break;
    }
    case rapidjson::kNullType:
    default:
        ddwaf_object_invalid(object);
        break;
    }
}

ddwaf_object read_rule_file(string_view filename)
{
    std::ifstream rule_file(filename.data(), std::ios::in);
    if (!rule_file) {
        throw std::system_error(errno, std::generic_category());
    }

    // Create a buffer equal to the file size
    rule_file.seekg(0, std::ios::end);
    std::string buffer(rule_file.tellg(), '\0');
    buffer.resize(rule_file.tellg());
    rule_file.seekg(0, std::ios::beg);

    auto buffer_size = buffer.size();
    if (buffer_size > static_cast<typeof(buffer_size)>(
                          std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error{"rule file is too large"};
    }

    rule_file.read(&buffer[0], static_cast<std::streamsize>(buffer.size()));
    buffer.resize(rule_file.gcount());
    rule_file.close();

    rapidjson::Document document;
    rapidjson::ParseResult result = document.Parse(buffer.data());
    if ((result == nullptr) || !document.IsObject()) {
        throw std::invalid_argument("invalid json rule");
    }

    ddwaf_object object;
    json_to_object(&object, document);

    return object;
}

}

waf_handle::waf_handle(ddwaf_object *ruleset) {
    handle_ = ddwaf_init(ruleset, nullptr, nullptr);
    if (handle_ == nullptr) {
        throw std::runtime_error("unknown error");
    }
}

waf_handle::~waf_handle() {
    if (handle_ != nullptr) {
        ddwaf_destroy(handle_);
    }
}


std::shared_ptr<waf_handle> security_library::handle_{nullptr};

void security_library::initialise_security_library()
{
    auto ruleset = read_rule_file("/tmp/ruleset.json");
    security_library::handle_ = std::make_shared<waf_handle>(&ruleset);
}

std::vector<string_view> security_library::environment_variable_names()
{
  return {// These environment variable names are taken from `tracer_options.cpp`
          // and `tracer.cpp` in the `dd-opentracing-cpp` repository.
          // I did `git grep '"DD_\w\+"' -- src/` in the `dd-opentracing-cpp`
          // repository.
          "DD_APPSEC_ENABLED",
          "DD_APPSEC_RULES"};
}

}
}
