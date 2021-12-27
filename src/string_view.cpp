#include "string_view.h"

#include <cstring>
#include <locale>

namespace std {

// I didn't think that C++14/11/98 exposed a general-purpose hash function, but
// it does, buried within the IO library's "locale" concept.
// `std::locale::classic()` is the "C" locale.
long hash<::datadog::nginx::string_view>::hash_bytes(const char *begin, std::size_t size) {
    return std::use_facet<std::collate<char>>(std::locale::classic()).hash(begin, begin + size);
}

}  // namespace std
