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

namespace datadog::nginx::security {

using namespace std::literals;

struct QueryStringIter {
  enum class trim_mode { no_trim, do_trim } trim;
  std::string_view qs;
  std::size_t pos{0};
  DdwafMemres &memres;
  std::unordered_set<std::string_view> interned_strings;
  unsigned char separator;

  QueryStringIter(std::string_view qs, DdwafMemres &memres,
                  unsigned char separator, trim_mode trim)
      : qs{qs}, memres{memres}, separator{separator}, trim{trim} {}

  QueryStringIter(const ngx_str_t &qs, DdwafMemres &memres,
                  unsigned char separator, trim_mode trim)
      : QueryStringIter{datadog::nginx::to_string_view(qs), memres, separator,
                        trim} {}

  void reset() noexcept { pos = 0; }

  bool operator!=(const QueryStringIter &other) const noexcept {
    return pos != other.pos;
  }

  bool ended() const noexcept { return pos == qs.length(); }

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

  bool is_delete() const { return false; }

  QueryStringIter &operator++() {
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
    if (trim == trim_mode::do_trim) {
      return decode_trim(sv);
    } else {
      return decode_no_trim(sv);
    }
  }

  std::string_view decode_no_trim(std::string_view sv) {
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

  std::string_view decode_trim(std::string_view sv) {
    auto result = decode_no_trim(sv);
    while (!result.empty() && std::isspace(result.front())) {
      result.remove_prefix(1);
    }
    while (!result.empty() && std::isspace(result.back())) {
      result.remove_suffix(1);
    }
    return result;
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

struct qs_iter_agg {
  std::vector<std::unique_ptr<QueryStringIter>> iters;
  std::size_t cur{0};

  void add(std::unique_ptr<QueryStringIter> iter) {
    iters.push_back(std::move(iter));
    if (cur == iters.size()) {
      if (iters[cur]->ended()) {
        cur++;
      }
    }
  }

  std::string_view cur_key() const { return iters[cur]->cur_key(); }

  bool is_delete() const { return false; }

  void reset() noexcept {
    for (size_t i = 0; i < iters.size(); i++) {
      iters[i]->reset();
    }
    cur = 0;
    while (cur < iters.size() && iters[cur]->ended()) {
      cur++;
    }
  }

  bool ended() const noexcept { return cur >= iters.size(); }

  std::pair<std::string_view, std::string_view> operator*() {
    return *(*iters[cur]);
  }

  qs_iter_agg &operator++() {
    iters[cur]->operator++();
    while (cur < iters.size() && iters[cur]->ended()) {
      cur++;
    }

    return *this;
  }
};
}  // namespace datadog::nginx::security
