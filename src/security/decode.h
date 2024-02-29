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
  std::string_view qs;
  std::size_t pos{0};
  ddwaf_memres &memres;
  std::unordered_set<std::string_view> interned_strings;

  query_string_iter(const ngx_str_t &qs, ddwaf_memres &memres)
      : qs{to_sv(qs)}, memres{memres} {}

  query_string_iter(std::string_view qs, ddwaf_memres &memres)
      : qs{qs}, memres{memres} {}

  void reset() noexcept { pos = 0; }

  bool operator!=(const query_string_iter &other) const noexcept {
    return pos != other.pos;
  }

  bool ended() const noexcept {
    return pos == qs.length();
  }

  std::pair<std::string_view, std::string_view> operator*() {
    std::string_view sv{rest()};

    auto amp = sv.find('&');
    if (amp != std::string_view::npos) {
      sv = sv.substr(0, amp);
    }

    auto eq = sv.find('=');
    if (eq == std::string_view::npos) {
      // no =
      return {decode(sv), ""sv};
    }
    if (eq == sv.size() - 1) {
      // foo=, with nothing after
      return {decode(sv.substr(0, eq)), ""sv};
    }

    return {sv.substr(0, eq), sv.substr(eq + 1, sv.size() - eq - 1)};
  }

  std::string_view cur_key() {
    std::string_view sv{rest()};

    auto amp = sv.find('&');
    if (amp != std::string_view::npos) {
      sv = sv.substr(0, amp);
    }

    auto eq = sv.find('=');
    if (eq == std::string_view::npos) {
      // no =
      return decode(sv);
    }

    return decode(sv.substr(0, eq));
  }

  query_string_iter &operator++() {
    auto amp = rest().find('&');
    if (amp == std::string_view::npos || amp == qs.size() - 1) {
      pos = qs.length();
    } else {
      pos += amp + 1;
    }
    return *this;
  }

 private:
  std::string_view rest() const noexcept { return qs.substr(pos); }

  std::string_view decode(std::string_view sv) {
    if (sv.empty()) {
      return ""sv;
    }

    auto perc = sv.find('%');
    if (perc == std::string_view::npos) {
      return sv;
    }

    // allocate the same size, because some % might not introduce a valid
    // hexadecimal sequence
    std::unique_ptr<unsigned char[]> buf{new unsigned char[sv.size()]};
    std::string_view r{sv};
    unsigned char *w = buf.get();
    while (true) {
      std::memcpy(w, r.data(), perc);
      w += perc;

      if (r.length() >= perc + 3) {
        auto num = sv.substr(perc + 1, 2);
        unsigned result;
        const char *end = num.data() + num.length();
        auto [ptr, ec] = std::from_chars(num.data(), end, result, 16);
        if (ec == std::errc{} && ptr == end) {
          *w = static_cast<unsigned char>(result);
          w++;
          sv.remove_prefix(perc + 3);
          perc = sv.find('%');
        } else {
          *w = '%';
          w++;
          sv.remove_prefix(perc + 1);
          perc = sv.find('%');
        }
        if (perc == std::string_view::npos) {
          std::memcpy(w, sv.data(), sv.length());
          w += sv.length();
          break;
        }
      } else {
        std::memcpy(w, r.data() + perc, r.length() - perc);
        w += r.length();
        break;
      }
    }

    std::string_view res{reinterpret_cast<char *>(buf.get()),
                         static_cast<std::uintptr_t>(w - buf.get())};

    auto interned = interned_strings.find(res);
    if (interned != interned_strings.end()) {
      return *interned;
    }

    auto *p = memres.allocate_string(res.length() + 1);
    std::memcpy(p, res.data(), res.length());
    p[res.length()] = '\0';

    std::string_view interned_res{p, res.length()};
    interned_strings.insert(p);
    return interned_res;
  }
};
}  // namespace security
}  // namespace nginx
}  // namespace datadog
