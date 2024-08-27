#include <security/ddwaf_memres.h>
#include <security/ddwaf_obj.h>
extern "C" {
#include <ngx_core.h>
}

namespace test {

class ManagedChain {
 public:
  ManagedChain(std::vector<std::string_view> &parts) {
    ngx_chain_t **cur = &base;
    for (auto &&p : parts) {
      auto *buf = new ngx_buf_t{};
      buf->start = buf->pos =
          reinterpret_cast<u_char *>(const_cast<char *>(p.data()));
      buf->last = buf->end = buf->pos + p.size();
      if (!*cur) {
        *cur = new ngx_chain_t{};
      }
      (*cur)->buf = buf;
      cur = &(*cur)->next;
    }
  }

  ~ManagedChain() {
    auto *cur = base;
    while (cur) {
      auto *next = cur->next;
      delete cur->buf;
      delete cur;
      cur = next;
    }
  }

  operator ngx_chain_t &() { return *base; }

  std::size_t size() {
    std::size_t s = 0;
    for (auto *cur = base; cur; cur = cur->next) {
      s += cur->buf->last - cur->buf->pos;
    }
    return s;
  }

 private:
  ngx_chain_t *base = new ngx_chain_t{};
};
}  // namespace test
