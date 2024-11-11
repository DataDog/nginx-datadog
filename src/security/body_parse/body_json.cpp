#include "body_json.h"

#include <rapidjson/error/en.h>
#include <rapidjson/reader.h>

#include <algorithm>

#include "../ddwaf_memres.h"
#include "../ddwaf_obj.h"

extern "C" {
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_log.h>
}

namespace {

namespace dnsec = datadog::nginx::security;
using dnsec::ddwaf_obj;

/* Adapt a nginx chain to a rapidjson input.
 * see https://rapidjson.org/classrapidjson_1_1_stream.html */
class RapidNgxChainInputStream {
 public:
  using Ch = char;

  RapidNgxChainInputStream(const ngx_chain_t *chain) : current_{chain} {
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
};

/* Rapidjson event handler for serializing into ddwaf_obj, allowing for
 * truncated input */
class ToDdwafObjHandler
    : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>,
                                          ToDdwafObjHandler> {
 public:
  ToDdwafObjHandler(ddwaf_obj &slot, dnsec::DdwafMemres &memres)
      : pool_{memres}, memres_{memres}, bufs_{{&slot, 0, 1}} {}

  ddwaf_obj *finish(ngx_http_request_t &req) {
    if (bufs_.size() != 1) {
      ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
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
      ngx_log_error(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                    "json parsing finished with multiple top-level objects");
    } else if (buf.len == 0) {
      // should not happen
      ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                     "json parsing finished without producing any object");
      return nullptr;
    }

    return &buf.cur_obj();
  }

  bool Null() {
    get_slot().make_null();
    return true;
  }

  bool Bool(bool b) {
    get_slot().make_bool(b);
    return true;
  }

  bool Int(int i) {
    get_slot().make_number(i);
    return true;
  }

  bool Uint(unsigned u) {
    get_slot().make_number(u);
    return true;
  }

  bool Int64(int64_t i) {
    get_slot().make_number(i);
    return true;
  }

  bool Uint64(uint64_t u) {
    get_slot().make_number(u);
    return true;
  }

  bool Double(double d) {
    get_slot().make_number(d);
    return true;
  }

  bool String(const char *str, rapidjson::SizeType length, bool copy) {
    std::string_view sv{str, length};
    get_slot().make_string(sv, memres_);
    return true;
  }

  bool Key(const char *str, rapidjson::SizeType length, bool copy) {
    std::string_view sv{str, length};
    get_slot_for_key().set_key(sv, memres_);

    return true;
  }

  bool StartObject() {
    push_map();
    return true;
  }

  bool EndObject(rapidjson::SizeType /*memberCount*/) {
    pop_container();
    return true;
  }

  bool StartArray() {
    push_array();
    return true;
  }

  bool EndArray(rapidjson::SizeType /*elementCount*/) {
    pop_container();
    return true;
  }

 private:
  dnsec::DdwafObjArrPool<ddwaf_obj> pool_;
  dnsec::DdwafMemres &memres_;
  struct Buf {
    ddwaf_obj *ptr;
    std::size_t len;
    std::size_t cap;
    bool key_last;

    auto cur_obj() -> ddwaf_obj & {
      assert(len > 0);
      return ptr[len - 1];
    }
  };
  std::vector<Buf> bufs_{{nullptr, 0, 0}};

  ddwaf_obj &get_slot() { return do_get_slot(false); }

  ddwaf_obj &get_slot_for_key() { return do_get_slot(true); }

  ddwaf_obj &do_get_slot(bool for_key) {
    auto &buf = bufs_.back();
    assert(!for_key || !buf.key_last);  // no two keys in succession
    if (buf.key_last) {
      auto &ret = buf.cur_obj();
      buf.key_last = false;
      return ret;
    }

    if (for_key) {
      buf.key_last = true;
    }

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

  void push_array() {
    auto &slot = get_slot();
    slot.type = DDWAF_OBJ_ARRAY;
    bufs_.emplace_back(Buf{nullptr, 0, 0});
  }

  void push_map() {
    auto &slot = get_slot();
    slot.type = DDWAF_OBJ_MAP;
    bufs_.emplace_back(Buf{nullptr, 0, 0});
  }

  void pop_container() {
    auto &buf_arr = bufs_.back();
    bufs_.pop_back();
    auto buf_cont = bufs_.back();
    ddwaf_obj &slot = buf_cont.cur_obj();
    slot.nbEntries = buf_arr.len;
    slot.array = buf_arr.ptr;
  }
};

}  // namespace

namespace datadog::nginx::security {

bool parse_json(ddwaf_obj &slot, ngx_http_request_t &req,
                const ngx_chain_t &chain, dnsec::DdwafMemres &memres) {
  // be as permissive as possible
  static constexpr unsigned parse_flags =
      rapidjson::kParseStopWhenDoneFlag |
      rapidjson::kParseEscapedApostropheFlag | rapidjson::kParseNanAndInfFlag |
      rapidjson::kParseTrailingCommasFlag | rapidjson::kParseCommentsFlag |
      rapidjson::kParseIterativeFlag;

  ToDdwafObjHandler handler{slot, memres};
  rapidjson::Reader reader;
  RapidNgxChainInputStream is{&chain};
  rapidjson::ParseResult res =
      reader.Parse<parse_flags, RapidNgxChainInputStream>(is, handler);
  ddwaf_obj *json_obj = handler.finish(req);
  if (res.IsError()) {
    if (json_obj) {
      ngx_log_debug1(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                     "json parsing failed after producing some output: %s",
                     rapidjson::GetParseError_En(res.Code()));
    } else {
      ngx_log_error(NGX_LOG_NOTICE, req.connection->log, 0,
                    "json parsing failed without producing any output: %s",
                    rapidjson::GetParseError_En(res.Code()));
    }
  } else {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                   "body json parsing finished successfully");
  }

  assert(json_obj == nullptr || json_obj == &slot);
  return json_obj != nullptr;
}

}  // namespace datadog::nginx::security
