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
  NgxChainInputStream(const ngx_chain_t *chain, std::size_t limit)
      : current_{chain}, left_{limit} {
    if (!current_) {
      throw std::invalid_argument{"chain must not be null"};
    }

    enter_buffer();
  }
  NgxChainInputStream(const NgxChainInputStream &) = default;
  NgxChainInputStream &operator=(const NgxChainInputStream &) = default;
  NgxChainInputStream(NgxChainInputStream &&) = delete;
  NgxChainInputStream &operator=(NgxChainInputStream &&) = delete;

  std::uint8_t operator*() {
    if (pos_ == end_) {
      if (!advance_buffer()) {
        assert("peek over eof" == 0);
        return 0;
      }
    }
    return *pos_;
  }

  std::size_t operator-(const NgxChainInputStream &other) const {
    return global_pos_ - other.global_pos_;
  }

  std::uint8_t read() {
    if (pos_ == end_) {
      if (!advance_buffer()) {
        assert("read over eof" == 0);
        return 0;
      }
    }
    global_pos_++;
    return *pos_++;
  }

  std::size_t read(std::uint8_t *buffer, size_t buf_size) {
    std::size_t read = 0;
    while (read < buf_size) {
      if (pos_ == end_) {
        if (!advance_buffer()) {
          return read;
        }
      }
      std::size_t read_now =
          std::min(static_cast<std::size_t>(end_ - pos_), buf_size - read);
      std::copy_n(pos_, read_now, buffer + read);
      pos_ += read_now;
      read += read_now;
    }
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

  bool eof() const {
    if (pos_ == end_) {
      return current_ == nullptr || current_->next == nullptr || left_ == 0;
    }
    return false;
  }

 private:
  bool advance_buffer() {
    if (left_ == 0 || !current_->next) {
      return false;
    }
    current_ = current_->next;
    enter_buffer();
    return true;
  }

  // the limit can fall in the middle of a buffer
  void enter_buffer() {
    pos_ = current_->buf->pos;
    auto const size =
        std::min(static_cast<std::size_t>(current_->buf->last - pos_), left_);
    end_ = pos_ + size;
    left_ -= size;
  }

  const ngx_chain_t *current_;
  u_char *pos_{};
  u_char *end_{};
  std::size_t global_pos_{};
  std::size_t left_;  // bytes we may still expose
};
}  // namespace datadog::nginx::security
