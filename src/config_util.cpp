#include "config_util.h"

#include <cassert>
#include <ios>
#include <limits>

namespace datadog {
namespace nginx {
namespace {

// Read a single-quoted string without escapes from the specified `input` and
// append it to the specified `output`.  Assign a diagnostic to the specified
// `error` if an error occurs.  Return `input`.  The initial single-quote
// character is expected to be missing from `input` (having already been
// consumed by the function that dispatched to this one).
std::istream& scan_single_quoted_string(std::istream& input, std::string& output,
                                        std::string& error) {
  output.push_back('\'');

  std::string content;
  std::getline(input, content, '\'');
  output.append(content);
  if (input.eof()) {
    error = "unterminated single-quoted string";
  } else {
    output.push_back('\'');
  }

  return input;
}

// Read a double-quoted string with backslash escapes from the specified
// `input` and append it to the specified `output`.  Assign a diagnostic to the
// specified `error` if an error occurs.  Return `input`.  The initial
// double-quote character is expected to be missing from `input` (having
// already been consumed by the function that dispatched to this one).
std::istream& scan_double_quoted_string(std::istream& input, std::string& output,
                                        std::string& error) {
  output.push_back('\"');

  char ch;
  while (input.get(ch)) {
    switch (ch) {
      case '\\':
        if (!input.get(ch)) {
          error = "trailing escape sequence \"\\\"";
          return input;
        }
        output.push_back('\\');
        output.push_back(ch);
        break;
      case '\"':
        output.push_back(ch);
        return input;
      default:
        output.push_back(ch);
    }
  }

  error = "unterminated double-quoted string";
  return input;
}

// Read a "#"-style line comment from the specified `input`.  If the specified
// `comment_policy` is `CommentPolicy::INCLUDE`, then append the comment to the
// specified `output`.  If `comment_policy` is `CommentPolicy::OMIT`, then do
// not modify `output`.  Assign a diagnostic to the specified `error` if an
// error occurs. Return `input`.  The initial "#" character is expected to be
// missing from `input` (having already been consumed by the function that
// dispatched to this one).
std::istream& scan_comment(std::istream& input, std::string& output, std::string& error,
                           CommentPolicy comment_policy) {
  switch (comment_policy) {
    case CommentPolicy::INCLUDE: {
      output.push_back('#');
      std::string content;
      std::getline(input, content);
      output.append(content);
      break;
    }
    default: {
      assert(comment_policy == CommentPolicy::OMIT);
      const auto max = std::numeric_limits<std::streamsize>::max();
      input.ignore(max, '\n');
    }
  }

  if (input) {
    output.push_back('\n');
  }

  return input;
}

}  // namespace

std::istream& scan_config_block(std::istream& input, std::string& output, std::string& error,
                                CommentPolicy comment_policy) {
  // Don't skip whitespace -- I want to echo the whitespace to `output`.
  // I could use the stream buffer (`input.rdbuf()`) directly to avoid all
  // formatting, but it's convenient to have `std::getline`.
  input >> std::noskipws;

  // `depth` is how far nested we are in curly braces.
  int depth = 1;  // the first "{" is assumed already consumed
  output.push_back('{');

  char ch;
  while (input.get(ch)) {
    switch (ch) {
      case '\"':
        scan_double_quoted_string(input, output, error);
        break;
      case '\'':
        scan_single_quoted_string(input, output, error);
        break;
      case '{':
        output.push_back(ch);
        ++depth;
        break;
      case '}':
        output.push_back(ch);
        if (--depth == 0) {
          // All open "{" are now closed.  We're done.
          return input;
        }
        break;
      case '#':
        scan_comment(input, output, error, comment_policy);
        break;
      default:
        output.push_back(ch);
    }
  }

  if (depth != 0 && error.empty()) {
    error = "unclosed curly brace";
  }
  return input;
}

}  // namespace nginx
}  // namespace datadog
