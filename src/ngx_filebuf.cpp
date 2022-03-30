#include "ngx_filebuf.h"

#include <algorithm>
#include <cassert>

namespace datadog {
namespace nginx {
namespace {

// Nginx uses `unsigned char`, while C++ streams prefer `char` (signedness is
// implementation-defined).  Here are conversion functions.  The world is two's
// complement, so it's fine.

char* cast(u_char* ptr) { return reinterpret_cast<char*>(ptr); }

u_char* cast(char* ptr) { return reinterpret_cast<u_char*>(ptr); }

}  // namespace

NgxFileBuf::NgxFileBuf(ngx_buf_t& buffer, ngx_file_t& file, std::string inputPrefix,
                       ngx_uint_t* newlines)
    : buffer(buffer), file(file), prefix(std::move(inputPrefix)), newlines(newlines) {
  // Start by using `prefix` as the buffer.  When we exhaust it, switch to
  // `buffer` (see `underflow`).
  char* const begin = &*prefix.begin();
  char* const end = &*prefix.end();
  setg(begin, begin, end);
  newlines_from = begin;
}

NgxFileBuf::~NgxFileBuf() {
  // Leave the `ngx_buf_t` consistent with this object.
  assert(buffer.start == cast(eback()));
  buffer.pos = cast(gptr());
  buffer.last = cast(egptr());

  // Finish counting newlines.
  if (newlines) {
    *newlines += std::count(newlines_from, gptr(), '\n');
  }
}

std::streambuf::int_type NgxFileBuf::underflow() {
  if (gptr() != egptr()) {
    // There's still buffer to consume.  Return a character.
    return traits_type::to_int_type(*gptr());
  }

  // If the buffer is the prefix (which we've just exhausted), switch to the
  // nginx `buffer` (after which we might not be underflowing anymore).
  if (eback() == &*prefix.begin()) {
    setg(cast(buffer.start), cast(buffer.pos), cast(buffer.last));
    newlines_from = cast(buffer.pos);
    return underflow();
  }

  // We've reached the end of the buffer.  Overwrite the buffer with
  // characters read from the file.

  // But first, update our count of newlines.
  if (newlines) {
    *newlines += std::count(newlines_from, gptr(), '\n');
  }

  const ssize_t n = ngx_read_file(&file, buffer.start, buffer.end - buffer.start, file.offset);
  if (n == 0) {
    newlines = nullptr;  // prevent double counting in the destructor
    return traits_type::eof();
  }
  if (n == NGX_ERROR) {
    newlines = nullptr;  // prevent double counting in the destructor
    return traits_type::eof();
  }

  // Reset the "get area" to have the same beginning as before, but now the
  // "current" pointer points to the beginning, and the "end" points to one
  // past what we just read.
  setg(eback(), eback(), eback() + n);
  newlines_from = eback();

  return traits_type::to_int_type(*gptr());
}

}  // namespace nginx
}  // namespace datadog
