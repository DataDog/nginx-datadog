#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

extern "C" {
#include <ngx_core.h>
}

namespace datadog::nginx::security {
class NgxChainInputStream {
 public:
  NgxChainInputStream(const ngx_chain_t *chain, std::size_t max_bytes)
      : current_{chain}, bytes_left_{max_bytes} {
    if (!current_) {
      throw std::invalid_argument{"chain must not be null"};
    }

    enter_buffer();
    skip_exhausted_buffers();
  }
  NgxChainInputStream(const NgxChainInputStream &) = default;
  NgxChainInputStream &operator=(const NgxChainInputStream &) = default;

  std::uint8_t operator*() const {
    if (eof()) {
      assert("peek over eof" == 0);
      return 0;
    }
    return *pos_;
  }

  std::uint8_t read() {
    if (eof()) {
      assert("read over eof" == 0);
      return 0;
    }
    read_count_++;
    std::uint8_t const ch = *pos_++;
    skip_exhausted_buffers();
    return ch;
  }

  std::size_t read(std::uint8_t *buffer, size_t buf_size) {
    std::size_t read = 0;
    while (read < buf_size && !eof()) {
      std::size_t read_now =
          std::min(static_cast<std::size_t>(end_ - pos_), buf_size - read);
      std::copy_n(pos_, read_now, buffer + read);
      pos_ += read_now;
      read += read_now;
      skip_exhausted_buffers();
    }
    read_count_ += read;
    return read;
  }

  /* Reads until end (not including it) or delim (including it), whichever comes
   * first */
  template <typename Iter>
  std::size_t read_until(Iter begin, Iter end, std::uint8_t delim) {
    // could be optimized
    auto w = begin;
    while (!eof() && w < end) {
      auto ch = read();
      *w++ = ch;
      if (ch == delim) {
        break;
      }
    }

    return w - begin;
  }

  // We never stay at the end of a buffer if more data follows
  bool eof() const { return pos_ == end_; }

  // Number of bytes read so far
  std::size_t position() const { return read_count_; }

 private:
  // Moves past used up and empty buffers, until one has data left
  void skip_exhausted_buffers() {
    while (pos_ == end_ && bytes_left_ > 0 && current_->next) {
      current_ = current_->next;
      enter_buffer();
    }
  }

  // max_bytes can end in the middle of a buffer
  void enter_buffer() {
    pos_ = current_->buf->pos;
    auto const size = std::min(
        static_cast<std::size_t>(current_->buf->last - pos_), bytes_left_);
    end_ = pos_ + size;
    bytes_left_ -= size;
  }

  const ngx_chain_t *current_;
  u_char *pos_{};
  u_char *end_{};
  std::size_t read_count_{};
  std::size_t bytes_left_;  // bytes we may still expose
};
}  // namespace datadog::nginx::security
