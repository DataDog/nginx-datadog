#pragma once

#include <cstdlib>
#include <memory>
#include <string_view>
#include <unordered_set>

extern "C" {
#include <ngx_core.h>
}

#include "ddwaf_memres.h"
#include "string_util.h"

namespace datadog::nginx::security {

using namespace std::literals;

std::string decode_urlencoded(std::string_view sv);

struct QueryStringIter {
  enum class trim_mode { no_trim, do_trim } trim;
  std::string_view qs;
  std::size_t pos{0};
  DdwafMemres &memres;
  std::unordered_set<std::string_view> interned_strings;
  unsigned char separator;

  QueryStringIter(std::string_view qs, DdwafMemres &memres,
                  unsigned char separator, trim_mode trim)
      : trim{trim}, qs{qs}, memres{memres}, separator{separator} {}

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
  std::pair<std::string_view, std::string_view> operator*();

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

  std::string_view decode_no_trim(std::string_view sv);

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

  std::string_view intern_string(std::string_view sv);
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
