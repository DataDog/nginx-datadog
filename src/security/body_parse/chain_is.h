#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>

extern "C" {
#include <ngx_core.h>
}

namespace datadog::nginx::security {
class NgxChainInputStream {
 public:
  NgxChainInputStream(const ngx_chain_t *chain) : current_{chain} {
    if (current_) {
      pos_ = current_->buf->pos;
      end_ = current_->buf->last;
    }
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
      return current_ == nullptr || current_->next == nullptr;
    }
    return false;
  }

 private:
  bool advance_buffer() {
    if (current_->next) {
      current_ = current_->next;
      pos_ = current_->buf->pos;
      end_ = current_->buf->last;
      return true;
    }
    return false;
  }

  const ngx_chain_t *current_;
  u_char *pos_{};
  u_char *end_{};
  std::size_t global_pos_{};
};
}  // namespace datadog::nginx::security
