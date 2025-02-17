#pragma once

extern "C" {
#include <ngx_core.h>
}
#include <cstddef>
#include <cstdint>
#include <optional>

template <std::size_t NBuffers, std::size_t BufferSize, std::uintptr_t Tag>
class BufferPool {
  static inline auto tag = reinterpret_cast<void *>(Tag);

 public:
  void update_chains(ngx_pool_t &pool, ngx_chain_t *out) noexcept {
    ngx_chain_t *out_copy = out;
    ngx_chain_update_chains(&pool, &free_, &busy_, &out_copy, tag);
  }

  std::optional<ngx_chain_t *> get_buffer(ngx_pool_t &pool) noexcept {
    if (free_) {
      ngx_chain_t *res = free_;
      free_ = res->next;
      res->next = nullptr;

      res->buf->recycled = 1;
      res->buf->pos = res->buf->start;
      res->buf->last = res->buf->start;
      res->buf->flush = 0;
      res->buf->sync = 0;
      res->buf->last_buf = 0;
      res->buf->last_in_chain = 0;
      return {res};
    }
    if (allocated_ >= NBuffers) {
      return std::nullopt;
    }

    ngx_buf_t *buf = ngx_create_temp_buf(&pool, BufferSize);
    if (!buf) {
      return std::nullopt;
    }
    auto chain = ngx_alloc_chain_link(&pool);
    if (!chain) {
      ngx_free_chain(&pool, chain);
      return std::nullopt;
    }
    buf->tag = tag;
    chain->buf = buf;
    chain->next = nullptr;
    allocated_++;
    return {chain};
  }

  ngx_chain_t *busy() const { return busy_; }

 private:
  ngx_chain_t *free_{};
  ngx_chain_t *busy_{};
  std::size_t allocated_{};
};
