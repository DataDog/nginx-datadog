#include "body_json.h"

#include <rapidjson/error/en.h>
#include <rapidjson/reader.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <version>

#include "../ddwaf_memres.h"
#include "../ddwaf_obj.h"
#include "../waf_input_budget.h"

extern "C" {
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_log.h>
}

namespace {

namespace dnsec = datadog::nginx::security;
using dnsec::ddwaf_obj;

dnsec::ddwaf_kv_obj *slots_to_entries(ddwaf_obj *slots,
                                      std::size_t num_entries);

/* Adapt a nginx chain to a rapidjson input.
 * see https://rapidjson.org/classrapidjson_1_1_stream.html */
class RapidNgxChainInputStream {
 public:
  using Ch = char;

  RapidNgxChainInputStream(const ngx_chain_t *chain, size_t limit)
      : current_{chain}, limit_{limit} {
    if (!current_) {
      throw std::invalid_argument{"chain must not be null"};
    }
    pos_ = current_->buf->pos;
    end_ = current_->buf->last;
  }

  Ch Peek() {  // NOLINT
    if (make_readable()) {
      return *pos_;
    }
    return '\0';
  }

  Ch Take() {  // NOLINT
    if (make_readable()) {
      read_++;
      return *pos_++;
    }
    return '\0';
  }

  std::size_t Tell() const {  // NOLINT
    return read_;
  }

  void Put(Ch) {  // NOLINT
                  // Not implemented because we're only reading
  }
  char *PutBegin() { return nullptr; }  // NOLINT
  size_t PutEnd(Ch *) { return 0; }     // NOLINT

 private:
  bool advance_buffer() {
    if (read_ >= limit_) {
      return false;
    }

    if (current_->next) {
      current_ = current_->next;
      pos_ = current_->buf->pos;
      end_ = current_->buf->last;
      return true;
    }
    return false;
  }

  bool make_readable() {
    while (pos_ == end_) {
      if (!advance_buffer()) {
        return false;
      }
    }
    return true;
  }

  const ngx_chain_t *current_;
  u_char *pos_{};
  u_char *end_{};
  std::size_t read_{};
  std::size_t limit_{};
};

/* Rapidjson event handler for serializing into ddwaf_obj, allowing for
 * truncated input. Returning false stops the parsing. */
class ToDdwafObjHandler
    : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>,
                                          ToDdwafObjHandler> {
 public:
  ToDdwafObjHandler(ddwaf_obj &slot, dnsec::WafInputBudget &budget,
                    dnsec::DdwafMemres &memres)
      : pool_{memres}, budget_{budget}, memres_{memres}, bufs_{{&slot, 0, 1}} {}

  ddwaf_obj *finish(const ngx_http_request_t &req) {
    if (bufs_.size() != 1) {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                    "json parsing finished prematurely");
      while (bufs_.size() > 1) {
        pop_container();
      }
    }

    if (bufs_.back().len == 0) {
      // should not happen
      return nullptr;
    }

    auto &buf = bufs_.back();
    if (buf.len > 1) {
      // should not happen
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                    "json parsing finished with multiple top-level objects");
    } else if (buf.len == 0) {
      // should not happen
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                    "json parsing finished without producing any object");
      return nullptr;
    }

    return &buf.cur_obj();
  }

  bool Null() {
    return add_scalar([](ddwaf_obj &obj) { obj.make_null(); });
  }

  bool Bool(bool b) {
    return add_scalar([b](ddwaf_obj &obj) { obj.make_bool(b); });
  }

  bool Int(int i) {
    return add_scalar([i](ddwaf_obj &obj) { obj.make_number(i); });
  }

  bool Uint(unsigned u) {
    return add_scalar([u](ddwaf_obj &obj) { obj.make_number(u); });
  }

  bool Int64(int64_t i) {
    return add_scalar([i](ddwaf_obj &obj) { obj.make_number(i); });
  }

  bool Uint64(uint64_t u) {
    return add_scalar([u](ddwaf_obj &obj) { obj.make_number(u); });
  }

  bool Double(double d) {
    return add_scalar([d](ddwaf_obj &obj) { obj.make_number(d); });
  }

  bool String(const char *str, rapidjson::SizeType length, bool /*copy*/) {
    return add_string({str, length});
  }

  bool Key(const char *str, rapidjson::SizeType length, bool /*copy*/) {
    // keys and values take consecutive slots: key, value, key, value...
    return add_string({str, length});
  }

  bool StartObject() { return start_container(ContainerType::MAP); }

  bool EndObject(rapidjson::SizeType /*memberCount*/) {
    end_container();
    return true;
  }

  bool StartArray() { return start_container(ContainerType::ARRAY); }

  bool EndArray(rapidjson::SizeType /*elementCount*/) {
    end_container();
    return true;
  }

 private:
  enum class ContainerType { MAP, ARRAY };

  dnsec::DdwafObjArrPool<ddwaf_obj> pool_;
  dnsec::WafInputBudget &budget_;
  dnsec::DdwafMemres &memres_;
  // > 0 while inside a container that is too deep: we drop its contents
  std::size_t skipped_depth_{0};
  struct Buf {
    ddwaf_obj *ptr;
    std::size_t len;
    std::size_t cap;

    auto cur_obj() const -> ddwaf_obj & {
      assert(len > 0);
      return ptr[len - 1];
    }
  };
  std::vector<Buf> bufs_{{nullptr, 0, 0}};

  // Whether the next slot is an array element or a map key.
  bool next_is_element() const {
    if (bufs_.size() == 1) {
      return false;  // top-level value
    }
    return !is_in_map() || bufs_.back().len % 2 == 0;
  }

  // Whether the innermost open container is a map. Needs one to be open.
  bool is_in_map() const {
    assert(bufs_.size() > 1);
    return bufs_[bufs_.size() - 2].cur_obj().is_map();
  }

  // Returns false to stop the parsing.
  template <typename Make>
  bool add_scalar(Make make) {
    if (skipped_depth_ > 0) {
      return true;  // inside a too-deep container: drop it
    }
    if (!budget_.charge_scalar(next_is_element())) {
      return false;  // budget exhausted
    }
    make(get_slot());
    return true;
  }

  bool add_string(std::string_view str) {
    if (skipped_depth_ > 0) {
      return true;
    }
    std::optional<std::string_view> kept =
        budget_.charge_string(str, next_is_element());
    if (!kept) {
      return false;
    }
    get_slot().make_string(*kept, memres_);
    return true;
  }

  bool start_container(ContainerType type) {
    if (skipped_depth_ > 0) {
      skipped_depth_++;
      return true;
    }
    if (!budget_.charge_container(next_is_element())) {
      return false;
    }
    // The slot gets an empty container. This is how we remember the type
    // while the container is open. pop_container then fills the slot.
    make_empty_container(get_slot(), type);
    // bufs_ has one entry for the top level, plus one per open container
    if (bufs_.size() > dnsec::WafInputBudget::kMaxContainerDepth) {
      // too deep: keep the container, but empty
      skipped_depth_ = 1;
    } else {
      // open a new buffer for the container's contents
      bufs_.emplace_back(Buf{nullptr, 0, 0});
    }
    return true;
  }

  static void make_empty_container(ddwaf_obj &slot, ContainerType type) {
    switch (type) {
      case ContainerType::MAP:
        slot.make_map(nullptr, 0);
        break;
      case ContainerType::ARRAY:
        slot.make_array(nullptr, 0);
        break;
    }
  }

  void end_container() {
    if (skipped_depth_ > 0) {
      skipped_depth_--;
      return;
    }
    pop_container();
  }

  ddwaf_obj &get_slot() {
    auto &buf = bufs_.back();
    if (buf.len < buf.cap) {
      buf.len++;
      return buf.ptr[buf.len - 1];
    }

    std::size_t new_cap = std::max(buf.cap * 2, 1UL);
    buf.ptr = pool_.realloc(buf.ptr, buf.cap, new_cap);

    buf.len++;
    buf.cap = new_cap;
    return buf.cur_obj();
  }

  void pop_container() {
    bool const is_map = is_in_map();
    if (is_map && bufs_.back().len % 2 != 0) {
      // truncated input: keep the last key, with a null value
      get_slot().make_null();
    }
    Buf buf_inner = bufs_.back();
    bufs_.pop_back();
    ddwaf_obj &slot = bufs_.back().cur_obj();
    if (is_map) {
      // a map buffer holds key, value, key, value...
      dnsec::ddwaf_kv_obj *entries =
          slots_to_entries(buf_inner.ptr, buf_inner.len / 2);
      slot.make_map(entries, buf_inner.len / 2);
    } else {
      slot.make_array(buf_inner.ptr, buf_inner.len);
    }
  }
};

// Turns key and value slots into map entries with the same contents. The
// slots stop existing; only the entries can be used afterwards.
dnsec::ddwaf_kv_obj *slots_to_entries(ddwaf_obj *slots,
                                      std::size_t num_entries) {
  if (num_entries == 0) {
    return nullptr;
  }
#ifdef __cpp_lib_start_lifetime_as
  return std::start_lifetime_as_array<dnsec::ddwaf_kv_obj>(slots, num_entries);
#else
  // Before C++23: memmove creates the entries in place, keeping the bytes.
  // launder gives us a pointer to them.
  void *storage =
      std::memmove(slots, slots, num_entries * sizeof(dnsec::ddwaf_kv_obj));
  return std::launder(static_cast<dnsec::ddwaf_kv_obj *>(storage));
#endif
}

}  // namespace

namespace datadog::nginx::security {

bool parse_json(ddwaf_obj &slot, const ngx_http_request_t &req,
                const ngx_chain_t &chain, size_t limit, WafInputBudget &budget,
                dnsec::DdwafMemres &memres) {
  // be as permissive as possible
  static constexpr unsigned parse_flags =
      rapidjson::kParseStopWhenDoneFlag |
      rapidjson::kParseEscapedApostropheFlag | rapidjson::kParseNanAndInfFlag |
      rapidjson::kParseTrailingCommasFlag | rapidjson::kParseCommentsFlag |
      rapidjson::kParseIterativeFlag;

  ToDdwafObjHandler handler{slot, budget, memres};
  rapidjson::Reader reader;
  RapidNgxChainInputStream is{&chain, limit};
  rapidjson::ParseResult res =
      reader.Parse<parse_flags, RapidNgxChainInputStream>(is, handler);
  ddwaf_obj *json_obj = handler.finish(req);
  if (budget.exhausted()) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                  "json body truncated: input budget exhausted");
  } else if (res.IsError()) {
    if (json_obj) {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                    "json parsing failed after producing some output: %s",
                    rapidjson::GetParseError_En(res.Code()));
    } else {
      ngx_log_error(NGX_LOG_NOTICE, req.connection->log, 0,
                    "json parsing failed without producing any output: %s",
                    rapidjson::GetParseError_En(res.Code()));
    }
  } else {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                  "body json parsing finished successfully");
  }

  assert(json_obj == nullptr || json_obj == &slot);
  return json_obj != nullptr;
}

}  // namespace datadog::nginx::security
