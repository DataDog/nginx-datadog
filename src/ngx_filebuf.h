#pragma once

// This component provides a `class`, `NgxFileBuf`, that implements the read
// portion of `std::streambuf`, reading data from a specified `ngx_file_t`, and
// using the specified `ngx_buf_t` for storage.  `NgxFileBuf` supports _reading
// only_, and additionally makes no use of locales.
//
// The purpose of `NgxFileBuf` is to act as an adapter between nginx's buffered
// file input facilities and those of standard C++.  For example, a JSON
// parsing library in C++ might accept a reference to a `std::istream` as
// input.  `NgxFileBuf` allows a `std::istream` to use a `ngx_file_t`, backed
// by a `ngx_buf_t`, as a source:
//
//     ngx_opentracing::NgxFileBuf buffer(...);
//     std::istream input(&buffer);
//     auto result = some::json_library::parse(input);
//     ...
//
// In particular, `NgxFileBuf` is used in conjunction with
// `ngx_opentracing::scan_config_block` to read a tracer's configuration
// as JSON directly from the nginx configuration file.  See `configure` in
// `opentracing_directive.cpp` for more information.

#include <streambuf>
#include <string>

extern "C" {
#include <ngx_core.h>
}

namespace ngx_opentracing {

// `NgxFileBuf` is a `std::streambuf` that uses a specified `ngx_file_t` as the
// source of data for read operations, and that uses a specified `ngx_buf_t`
// for buffering.  `NgxFileBuf` implements the read portion of `std::streambuf`
// only.
class NgxFileBuf : public std::streambuf {
  // `buffer` is the storage for the buffer.  `buffer`'s "get" pointer
  // (`.cur`) is also kept in sync with this object's pointer (`.gptr()`).
  ngx_buf_t& buffer;
  // `file` is the file from which characters will be read when this object
  // underflows.
  ngx_file_t& file;
  // `newlines`, unless `nullptr`, refers to a count of encountered newlines.
  // When this object underflows, and when it is destroyed, `*newlines` is
  // incremented by the number of line feeds gotten (i.e. treaded by the"get"
  // pointer) since the previous increment.
  ngx_uint_t* newlines;
  // `prefix` is the initial source of characters for this object. When it is
  // exhausted, `file` is used from then on.  `prefix` exists to work around
  // the fact that when nginx parses a configuration directive of type
  // `NGX_CONF_BLOCK`, it consumes the initial "{" of the block before
  // passing control to the registered handler.  Subsequent parsing can
  // either anticipate the missing "{", or, alternatively, `prefix` can be
  // used to "put it back" (i.e. `prefix == "{"`).
  std::string prefix;

 public:
  // Create a `NgxFileBuf` object using the specified `buffer` for storage,
  // reading characters from the specified `file` after the specified
  // `prefix` is first exhausted, and incrementing the number of encountered
  // newlines through the optionally specified `newlines`.  Note that
  // `newlines` might be incremented each time this buffer underflows, and
  // in the destructor.
  NgxFileBuf(ngx_buf_t& buffer, ngx_file_t& file, std::string prefix,
             std::size_t* newlines = nullptr);

  // Complete any remaining accounting of buffer pointers and newline
  // counts, and then destroy this object.
  ~NgxFileBuf();

 private:
  // Return the next available character, reading from `file` if necessary.
  // Return `traits_type::eof()` if the input file is exhausted or if an
  // error occurs.
  int_type underflow() override;
};

}  // namespace ngx_opentracing
