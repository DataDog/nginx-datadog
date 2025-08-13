#include <optional>
#include <string>

extern "C" {
#define ZLIB_CONST
#include <zlib.h>
}

namespace {
size_t estimate_compressed_size(size_t in_len) {
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  return (((size_t)((double)in_len * (double)1.015)) + 10 + 8 + 4 + 1);
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}
}  // namespace

namespace datadog::nginx::security {
inline std::optional<std::string> compress(const std::string_view &text) {
  std::string ret_string;
  z_stream strm = {};

  if (text.length() == 0) {
    return std::nullopt;
  }

  static constexpr auto window_bits = 15 | 0x10 /* for gzip */;
  if (Z_OK == deflateInit2(&strm, -1, Z_DEFLATED, window_bits, MAX_MEM_LEVEL,
                           Z_DEFAULT_STRATEGY)) {
    auto size = estimate_compressed_size(text.length());
    ret_string.resize(size);

    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    strm.next_in = reinterpret_cast<const Bytef *>(text.data());
    strm.next_out = reinterpret_cast<Bytef *>(ret_string.data());
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    strm.avail_in = text.length();
    strm.avail_out = size;

    if (Z_STREAM_END == deflate(&strm, Z_FINISH)) {
      deflateEnd(&strm);
      /* size buffer down to actual length */
      ret_string.resize(strm.total_out);
      ret_string.shrink_to_fit();
      return ret_string;
    }
    deflateEnd(&strm);
  }
  return std::nullopt;
}
}  // namespace datadog::nginx::security
