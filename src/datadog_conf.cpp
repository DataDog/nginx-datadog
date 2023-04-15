#include "datadog_conf.h"
#include "string_util.h"

namespace datadog {
namespace nginx {

bool operator==(const conf_directive_source_location_t& left, const conf_directive_source_location_t& right) {
    return str(left.file_name) == str(right.file_name) && \
                left.line == right.line && \
                str(left.directive_name) == str(right.directive_name);
}

std::string datadog_sample_rate_condition_t::tag_name() const {
    return "nginx.sample_rate_source";
}

std::string datadog_sample_rate_condition_t::tag_value() const {
    // e.g. "/etc/nginx/nginx.conf:23#1"
    std::string result;
    result += str(directive.file_name);
    result += ':';
    result += std::to_string(directive.line);
    result += '#';
    result += std::to_string(same_line_index + 1);  // one-based
    return result;
}

} // namespace datadog
} // namespace nginx
