#include "body_multipart.h"

#include <map>
#include <string_view>
#include <utility>

#include "../ddwaf_memres.h"
#include "../ddwaf_obj.h"
#include "chain_is.h"
#include "header.h"

extern "C" {
#include <ngx_core.h>
}

namespace dnsec = datadog::nginx::security;

namespace {
enum class LineType { BOUNDARY, BOUNDARY_END, OTHER, END_OF_FILE };

/*
 * Returns a lambda that reads data line by line.
 *
 * If the return is LineType::BOUNDARY or LineType::BOUNDARY_END, the boundary
 * was found in the begginning of the current line. It consumes the full line,
 * regardless of its size.
 *
 * If the return is LineType::END_OF_FILE, no data was read; we reached EOF.
 *
 * If the return is LineType::OTHER, the boundary is not found, and the line is
 * fully read (and possibly stored in the append string, if given), until
 * either LF or EOF is found.
 */
auto bind_consume_line(dnsec::HttpContentType &ct) {
  std::size_t beg_bound_size = 2 /* -- */ + ct.boundary.size();
  auto bound_buf = std::make_unique<std::uint8_t[]>(beg_bound_size);

  return [&ct, bound_buf = std::move(bound_buf), beg_bound_size](
             dnsec::NgxChainInputStream &is, std::string *append) {
    std::size_t read =
        is.read_until(bound_buf.get(), bound_buf.get() + beg_bound_size, '\n');
    if (read == 0) {
      return LineType::END_OF_FILE;
    }
    if (bound_buf[read - 1] == '\n') {
      // line too small; can't be boundary. The buffer is not long enough to
      // include the LF in --boundary\n
      if (append) {
        std::copy_n(bound_buf.get(), read, std::back_inserter(*append));
      }
      return LineType::OTHER;
    }

    // the input may have been truncated (we don't buffer the whole request)
    // so assume we saw a boundary if we see at least part of it
    if (is.eof() && read < beg_bound_size) {
      bool matched = true;
      for (std::size_t i = 0; matched && i < read; i++) {
        if (i < 2) {
          matched = bound_buf[i] == '-';
        } else {
          matched = bound_buf[i] == ct.boundary[i - 2];
        }
      }

      if (matched) {
        return LineType::BOUNDARY_END;
      }
    }

    if (read == beg_bound_size && std::memcmp(bound_buf.get(), "--", 2) == 0 &&
        std::memcmp(bound_buf.get() + 2, ct.boundary.data(),
                    ct.boundary.size()) == 0) {
      // we found the boundary. It doesn't matter if the line contains
      // extra characters (see RFC 2046)
      std::uint8_t ch{};
      LineType res;
      if (!is.eof() && (ch = is.read()) == '-' && !is.eof() &&
          ((ch = is.read()) == '-')) {
        res = LineType::BOUNDARY_END;
      } else {
        res = LineType::BOUNDARY;
      }

      // discard the rest of the line
      if (ch != '\n') {
        while (!is.eof() && is.read() != '\n') {
        }
      }

      return res;
    } else {
      // not a boundary
      if (append) {
        std::copy_n(bound_buf.get(), read, std::back_inserter(*append));
        while (!is.eof()) {
          std::uint8_t ch = is.read();
          append->push_back(ch);
          if (ch == '\n') {
            break;
          }
        }
      } else {
        while (!is.eof() && is.read() != '\n') {
        }
      }
      return LineType::OTHER;
    }
  };
}

struct Buf {
  dnsec::ddwaf_obj *ptr;
  std::size_t len;
  std::size_t cap;

  void extend(dnsec::DdwafObjArrPool<dnsec::ddwaf_obj> &pool) {
    std::size_t new_cap = cap * 2;
    if (new_cap == 0) {
      new_cap = 1;
    }
    ptr = pool.realloc(ptr, cap, new_cap);
    cap = new_cap;
  }

  dnsec::ddwaf_obj &new_slot(dnsec::DdwafObjArrPool<dnsec::ddwaf_obj> &pool) {
    if (len == cap) {
      extend(pool);
    }
    return ptr[len++];
  }
};

void remove_final_crlf(std::string &content) {
  // support also terminations with plain LF instead of CRLF
  if (content.size() >= 1 && content.back() == '\n') {
    content.pop_back();
    if (content.size() >= 1 && content.back() == '\r') {
      content.pop_back();
    }
  }
}
}  // namespace

namespace datadog::nginx::security {

bool parse_multipart(ddwaf_obj &slot, ngx_http_request_t &req,
                     HttpContentType &ct, const ngx_chain_t &chain,
                     DdwafMemres &memres) {
  if (ct.boundary.size() == 0) {
    ngx_log_error(NGX_LOG_NOTICE, req.connection->log, 0,
                  "multipart boundary is invalid: %s", ct.boundary.c_str());
  } else {
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                   "multipart boundary: %s", ct.boundary.c_str());
  }
  NgxChainInputStream stream{&chain};

  auto consume_line = bind_consume_line(ct);

  // find first boundary, discarding everything before it
  while (!stream.eof()) {
    auto line_type = consume_line(stream, nullptr);
    if (line_type == LineType::BOUNDARY) {
      break;
    } else if (line_type == LineType::BOUNDARY_END) {
      ngx_log_error(NGX_LOG_NOTICE, req.connection->log, 0,
                    "multipart: found end boundary before first boundary");
      return false;
    }
  }

  if (stream.eof()) {
    ngx_log_error(NGX_LOG_NOTICE, req.connection->log, 0,
                  "multipart: eof right after first boundary");
    return false;
  }

  DdwafObjArrPool<ddwaf_obj> pool{memres};
  std::map<std::string, Buf> data;

start_part:
  // headers after the previous boundary
  std::optional<MimeContentDisposition> cd =
      MimeContentDisposition::for_stream(stream);
  if (!cd) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                   "multipart: did not find Content-Disposition header");
  }

  // content
  {
    std::string content;
    while (true) {
      auto line_type = consume_line(stream, &content);

      if (line_type == LineType::OTHER) {
        continue;
      }

      // o/wise finished content (boundary/boundary_end/eof)

      if (line_type == LineType::BOUNDARY ||
          line_type == LineType::BOUNDARY_END) {
        // the \r\n preceding the boundary is deemed part of the boundary
        remove_final_crlf(content);
      }

      if (line_type == LineType::END_OF_FILE) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                       "multipart: eof before end boundary");
        // we could have been followed by a boundary that was truncated,
        // so remove final CRLF, LF, or CR
        remove_final_crlf(content);
        if (content.size() >= 1 && content.back() == '\r') {
          content.pop_back();
        }
      }

      if (cd) {
        auto &buf = data[cd->name];
        buf.new_slot(pool).make_string({content.data(), content.size()},
                                       memres);
      }

      if (line_type == LineType::BOUNDARY && !stream.eof()) {
        goto start_part;
      }

      break;
    }
  }

  if (data.empty()) {
    return false;
  }

  auto &map = slot.make_map(data.size(), memres);
  std::size_t i = 0;
  for (auto &[key, buf] : data) {
    auto &map_slot = map.at_unchecked(i++);
    map_slot.set_key(key, memres);
    if (buf.len == 1) {
      // if only one element, put the string directly under that key
      map_slot.shallow_copy_val_from(buf.ptr[0]);
    } else {
      map_slot.make_array(buf.ptr, buf.len);
    }
  }

  return true;
}

}  // namespace datadog::nginx::security
