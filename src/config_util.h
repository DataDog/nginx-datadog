#pragma once

// This component provides utilities for parsing tracing-specific directives
// from an nginx configuration file.

#include <istream>
#include <string>

namespace datadog {
namespace nginx {

// `CommentPolicy` is used to specify to `scan_config_block` whether to
// ignore comments as it scans the configuration file.
enum class CommentPolicy {
  OMIT,    // skip comments; do not include them in the output
  INCLUDE  // include comments in the output
};

// Read a balanced-curly-brace delimited block of text from the specified
// `input`, where the initial "{" has already been consumed (i.e. is not in
// `input`).  Append the text to the specified `output`.  Assign a diagnostic
// to the specified `error` if an error occurs.  If the specified
// `comment_policy` is `CommentPolicy::OMIT`, skip "#" comments when appending
// to `output`. Return `input`.
//
// A "balanced-curly-brace delimited block of text," as read by
// `scan_config_block`, satisfies the "block" production of the following
// grammar:
//
//     block  ::=  "{" entity* "}"
//     entity  ::=  block | comment | quote | other
//     comment  ::=  "#" [^\n]* "\n"
//     quote  ::=  single-quoted | double-quoted
//     single-quoted  ::=  "'"" [^']* "'""
//     double-quoted  ::=  "\"" ([^""] | \\.)* "\""
//     other  ::=  [^{}#"']+
//
// The language is more permissive than JSON, and supports most curly brace
// delimited YAML.
std::istream& scan_config_block(std::istream& input, std::string& output,
                                     std::string& error,
                                     CommentPolicy comment_policy);

}  // namespace nginx
}  // namespace datadog
