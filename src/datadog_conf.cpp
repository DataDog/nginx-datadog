#include "datadog_conf.h"
#include "string_util.h"

namespace datadog {
namespace nginx {

bool operator==(const conf_directive_source_location_t& left, const conf_directive_source_location_t& right) {
    return str(left.file_name) == str(right.file_name) && \
                left.line == right.line && \
                str(left.directive_name) == str(right.directive_name);
}

} // namespace datadog
} // namespace nginx
