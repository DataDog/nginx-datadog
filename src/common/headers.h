#pragma once

extern "C" {
#include <ngx_core.h>
#include <ngx_hash.h>
}

#include <string_view>

namespace datadog::common {

/// Searches through an NGINX header list to find a header with a matching key.
///
/// @param headers
///     A reference to an NGINX-style list (`ngx_list_t`) containing
///     `ngx_table_elt_t` elements, typically representing HTTP headers.
///
/// @param key
///     A string view representing the name of the header to search for.
///     The comparison is typically case-insensitive.
///
/// @return
///     A pointer to the matching `ngx_table_elt_t` header element if found,
///     or `nullptr` if no header with the given key exists in the list.
////
ngx_table_elt_t* search_header(ngx_list_t& headers, std::string_view key);

/// Adds a new HTTP header to an NGINX-style header list.
///
/// @param pool
///     A reference to the NGINX memory pool (`ngx_pool_t`) used for allocating
///     memory for the new header and its key/value strings. This pool must
///     remain valid for the lifetime of the header.
///
/// @param headers
///     A reference to an `ngx_list_t` representing the list of HTTP headers
///     (`ngx_table_elt_t` elements) to which the new header will be appended.
///
/// @param key
///     A string view representing the header name (e.g., "Content-Type").
///     This will be copied into the NGINX pool memory before insertion.
///
/// @param value
///     A string view representing the header value (e.g., "application/json").
///     This will also be copied into the NGINX pool memory.
///
/// @return
///     `true` if the header was successfully added to the list;
///     `false` if memory allocation failed or the list could not be updated.
bool add_header(ngx_pool_t& pool, ngx_list_t& headers, std::string_view key,
                std::string_view value);

/// Deletes a request header with the specified key from a NGINX-request header
/// list.
///
/// @param headers
///     A reference to an NGINX-style list (`ngx_list_t`) containing
///     `ngx_table_elt_t` elements, typically representing HTTP headers.
///
/// @param key
///     A string view representing the name of the header to delete.
///     The comparison is case-insensitive.
///
/// @return
///     `true` if a header with the given key was found and deleted;
///     `false` if no header with the given key exists in the list.
bool remove_header(ngx_list_t& headers, std::string_view key);

}  // namespace datadog::common
