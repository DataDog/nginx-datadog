#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <unordered_set>

extern "C" {
#include <ngx_string.h>
}

#include "ddwaf_memres.h"
#include "util.h"

namespace datadog {
namespace nginx {
namespace security {

using namespace std::literals;

struct query_string_iter {
  unsigned char separator;
  std::string_view qs;
  std::size_t pos{0};
  ddwaf_memres &memres;
  std::unordered_set<std::string_view> interned_strings;

  query_string_iter(std::string_view qs, ddwaf_memres &memres, unsigned char separator)
      : qs{qs}, memres{memres}, separator{separator} {}

  query_string_iter(const ngx_str_t &qs, ddwaf_memres &memres, unsigned char separator)
      : query_string_iter{to_sv(qs), memres, separator} {}

  void reset() noexcept { pos = 0; }

  bool operator!=(const query_string_iter &other) const noexcept {
    return pos != other.pos;
  }

  bool ended() const noexcept {
    return pos == qs.length();
  }

  // this may return empty keys and/or values, e.g. ?a=&=v&
  std::pair<std::string_view, std::string_view> operator*() {
    std::string_view kv{rest()};

    auto sep_pos = kv.find(separator);
    if (sep_pos != std::string_view::npos) {
      kv = kv.substr(0, sep_pos);
    }

    auto eq_pos = kv.find('=');
    if (eq_pos == std::string_view::npos) {
      // no =
      return {decode(kv), ""sv};
    }
    if (eq_pos == kv.size() - 1) {
      // foo=, with nothing after
      return {decode(kv.substr(0, eq_pos)), ""sv};
    }

    return {decode(kv.substr(0, eq_pos)),
            decode(kv.substr(eq_pos + 1, kv.size() - eq_pos - 1))};
  }

  std::string_view cur_key() {
    std::string_view kv{rest()};

    auto sep_pos = kv.find(separator);
    if (sep_pos != std::string_view::npos) {
      kv = kv.substr(0, sep_pos);
    }

    auto eq_pos = kv.find('=');
    if (eq_pos == std::string_view::npos) {
      // no =
      return decode(kv);
    }

    return decode(kv.substr(0, eq_pos));
  }

  query_string_iter &operator++() {
    auto sep_pos = rest().find(separator);
    if (sep_pos == std::string_view::npos) {
      pos = qs.length();
    } else {
      pos += sep_pos + 1;
    }
    return *this;
  }

 private:
  std::string_view rest() const noexcept { return qs.substr(pos); }

  std::string_view decode(std::string_view sv) {
    if (sv.empty()) {
      return ""sv;
    }

    auto perc_or_plus = sv.find_first_of("%+");
    if (perc_or_plus == std::string_view::npos) {
      return sv;
    }

    // allocate the same size; this may be an overstimation
    std::unique_ptr<unsigned char[]> buf{new unsigned char[sv.size()]};
    enum class state { normal, percent, percent1 } state = state::normal;
    const unsigned char *r = reinterpret_cast<const unsigned char *>(sv.data());
    const unsigned char *end = r + sv.size();
    unsigned char *w = reinterpret_cast<unsigned char *>(buf.get());
    for (; r < end; r++) {
      switch (state) {
        case state::normal:
          if (*r == '%') {
            state = state::percent;
          } else {
            *w++ = decode_plus(*r);
          }
          break;
        case state::percent:
          if (std::isxdigit(*r)) {
            state = state::percent1;
          } else {
            *w++ = '%';
            *w++ = decode_plus(*r);
            state = state::normal;
          }
          break;
        case state::percent1:
          if (std::isxdigit(*r)) {
            unsigned result;
            // can't fail
            std::from_chars(reinterpret_cast<const char *>(r - 1),
                            reinterpret_cast<const char *>(r + 1), result, 16);
            *w++ = static_cast<unsigned char>(result);
          } else {
            *w++ = '%';
            *w++ = *(r - 1);
            *w++ = decode_plus(*r);
          }
          state = state::normal;
          break;
      }
    }
    if (state == state::percent) {
      *w++ = '%';
    } else if (state == state::percent1) {
      *w++ = '%';
      *w++ = *(sv.data() + sv.size() - 1);
    }

    std::string_view res{reinterpret_cast<char *>(buf.get()),
                         static_cast<std::uintptr_t>(w - buf.get())};
    return intern_string(res);
  }

  unsigned char decode_plus(unsigned char c) {
    if (c == '+') {
      return ' ';
    }
    return c;
  }

  std::string_view intern_string(std::string_view sv) {
    auto interned = interned_strings.find(sv);
    if (interned != interned_strings.end()) {
      return *interned;
    }

    auto *p = memres.allocate_string(sv.length() + 1);
    std::memcpy(p, sv.data(), sv.length());
    p[sv.length()] = '\0';

    std::string_view interned_res{p, sv.length()};
    interned_strings.insert(p);
    return interned_res;
  }
};
}  // namespace security
}  // namespace nginx
}  // namespace datadog
