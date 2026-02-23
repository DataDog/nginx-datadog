#include "decode.h"

#include <charconv>
#include <cstring>
#include <iterator>

namespace {

inline unsigned char decode_plus(unsigned char c) {
  if (c == '+') {
    return ' ';
  }
  return c;
}

template <bool DecodePlus>
std::string decode_percent(std::string_view sv) {
  // allocate the same size; this may be an overstimation
  std::string result;
  result.reserve(sv.size());
  std::unique_ptr<unsigned char[]> buf{new unsigned char[sv.size()]};
  enum class state { NORMAL, PERCENT, PERCENT1 } state = state::NORMAL;
  const unsigned char *r = reinterpret_cast<const unsigned char *>(sv.data());
  const unsigned char *end = r + sv.size();
  auto w = std::back_inserter(result);
  for (; r < end; r++) {
    switch (state) {
      case state::NORMAL:
        if (*r == '%') {
          state = state::PERCENT;
        } else {
          w = DecodePlus ? decode_plus(*r) : *r;
        }
        break;
      case state::PERCENT:
        if (std::isxdigit(*r)) {
          state = state::PERCENT1;
        } else {
          w = '%';
          w = decode_plus(*r);
          state = state::NORMAL;
        }
        break;
      case state::PERCENT1:
        if (std::isxdigit(*r)) {
          unsigned result{};
          // can't fail
          std::from_chars(reinterpret_cast<const char *>(r - 1),
                          reinterpret_cast<const char *>(r + 1), result, 16);
          w = static_cast<unsigned char>(result);
        } else {
          w = '%';
          w = *(r - 1);
          w = DecodePlus ? decode_plus(*r) : *r;
        }
        state = state::NORMAL;
        break;
    }
  }
  if (state == state::PERCENT) {
    w = '%';
  } else if (state == state::PERCENT1) {
    w = '%';
    w = *(sv.data() + sv.size() - 1);
  }

  return result;
}

}  // namespace

namespace datadog::nginx::security {

std::string decode_urlencoded(std::string_view sv) {
  return decode_percent<false>(sv);
}

std::pair<std::string_view, std::string_view> QueryStringIter::operator*() {
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

std::string_view QueryStringIter::decode_no_trim(std::string_view sv) {
  if (sv.empty()) {
    return ""sv;
  }

  auto perc_or_plus = sv.find_first_of("%+");
  if (perc_or_plus == std::string_view::npos) {
    return sv;
  }

  std::string result{decode_percent<true>(sv)};
  return intern_string(result);
}

std::string_view QueryStringIter::intern_string(std::string_view sv) {
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
}  // namespace datadog::nginx::security
