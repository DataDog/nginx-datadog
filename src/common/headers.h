#pragma once

extern "C" {
#include <ngx_hash.h>
#include <ngx_core.h>
}

#include <string_view>

namespace datadog::common {

/// TBD
ngx_table_elt_t *search_header(ngx_list_t &headers, std::string_view key);

/// TBD
bool add_header(ngx_pool_t& pool, ngx_list_t &headers, std::string_view key,
                std::string_view value);

}  // namespace datadog::common
