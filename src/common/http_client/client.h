#pragma once

#include <nghttp2/nghttp2.h>

#include <functional>
#include <string_view>
#include <unordered_map>

extern "C" {
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
}

#include "string_util.h"

namespace datadog {
namespace common {
namespace http {

#define ARRLEN(x) (sizeof(x) / sizeof(x[0]))
#define MAKE_NV(NAME, VALUE, VALUELEN)                           \
  {                                                              \
    (uint8_t*)NAME, (uint8_t*)VALUE, sizeof(NAME) - 1, VALUELEN, \
        NGHTTP2_NV_FLAG_NONE                                     \
  }

#define MAKE_NV2(NAME, VALUE)                                             \
  {                                                                       \
    (uint8_t*)NAME, (uint8_t*)VALUE, sizeof(NAME) - 1, sizeof(VALUE) - 1, \
        NGHTTP2_NV_FLAG_NONE                                              \
  }

void resolve_handler(ngx_resolver_ctx_t*);
void conn_read_handler(ngx_event_t* wev);
void conn_write_handler(ngx_event_t* wev);
void conn_noop_handler(ngx_event_t*);

struct Response final {
  uint16_t status;
  std::string body;
  std::unordered_map<std::string, std::string> headers;
};

struct ClientCtx final {
  nghttp2_session* session;
  ngx_connection_t* conn;
  int32_t stream_id;
  std::unordered_map<std::string, std::string> headers;
  std::function<void(Response&&)> on_response;
  std::function<void(std::string&&)> on_error;
};

// nghttp2 callbacks
static nghttp2_ssize send_callback(nghttp2_session* session,
                                   const uint8_t* data, size_t length,
                                   int flags, void* user_data) {
  // TODO: regulate the amount of buffered data
  auto ctx = (ClientCtx*)user_data;
  ngx_connection_t* conn = ctx->conn;
  size_t sent = ngx_recv(conn, (u_char*)data, length);
  return (nghttp2_ssize)sent;
}

static int on_header_callback(nghttp2_session* session,
                              const nghttp2_frame* frame, const uint8_t* name,
                              size_t namelen, const uint8_t* value,
                              size_t valuelen, uint8_t flags, void* user_data) {
  // NOTE: Called when the server respond
  auto ctx = (ClientCtx*)user_data;
  switch (frame->hd.type) {
    case NGHTTP2_HEADERS:
      if (frame->headers.cat == NGHTTP2_HCAT_RESPONSE &&
          ctx->stream_id == frame->hd.stream_id) {
        ctx->headers.emplace(std::string((char*)name, namelen),
                             std::string((char*)value, valuelen));
        break;
      }
  }
  return 0;
}

// end nghttp2 callbacks

inline void resolve(ngx_resolver_t* resolver, std::string_view host) {
  ngx_resolver_ctx_t temp;
  /*temp.name = nginx::to_ngx_str(host);*/
  temp.name = ngx_string("localhost");

  ngx_resolver_ctx_t* resolve = ngx_resolve_start(resolver, &temp);
  if (resolve == nullptr) {
    // TODO: handle error
  }

  if (resolve == NGX_NO_RESOLVER) {
    int a = 1;
    (void)a;
    /*if (ctx->naddrs == 0) {*/
    /*  ngx_log_error(NGX_LOG_ERR, ctx->log, 0,*/
    /*                "no resolver defined to resolve %V", &ctx->host);*/
    /**/
    /*  ngx_ssl_ocsp_error(ctx);*/
    /*  return;*/
    /*}*/

    /*ngx_log_error(NGX_LOG_WARN, ctx->log, 0,*/
    /*              "no resolver defined to resolve %V", &ctx->host);*/
    /*goto connect;*/
    // TODO: Handle
  }

  resolve->name = temp.name;
  resolve->handler = resolve_handler;

  if (ngx_resolve_name(resolve) != NGX_OK) {
    // TODO: Handle error
    return;
  }
}

inline void resolve_handler(ngx_resolver_ctx_t* resolver) {
  if (resolver->state) {
    // TODO: Handle error
    /*ngx_log_error(NGX_LOG_ERR, ctx->log, 0, "%V could not be resolved (%i:
     * %s)",*/
    /*              &resolve->name, resolve->state,*/
    /*              ngx_resolver_strerror(resolve->state));*/
    goto resolve_done;
  }

  for (size_t i = 0; i < resolver->naddrs; ++i) {
    /*auto socklen = resolver->addrs[i].socklen;*/

    /*sockaddr = ngx_palloc(ctx->pool, socklen);*/
    /*if (sockaddr == NULL) {*/
    /*  goto failed;*/
    /*}*/
    /**/
    /*ngx_memcpy(sockaddr, resolve->addrs[i].sockaddr, socklen);*/
    /*ngx_inet_set_port(sockaddr, ctx->port);*/
    /**/
    /*ctx->addrs[i].sockaddr = sockaddr;*/
    /*ctx->addrs[i].socklen = socklen;*/
    /**/
    /*p = ngx_pnalloc(ctx->pool, NGX_SOCKADDR_STRLEN);*/
    /*if (p == NULL) {*/
    /*  goto failed;*/
    /*}*/
    /**/
    /*len = ngx_sock_ntop(sockaddr, socklen, p, NGX_SOCKADDR_STRLEN, 1);*/
    /**/
    /*ctx->addrs[i].name.len = len;*/
    /*ctx->addrs[i].name.data = p;*/
  }

resolve_done:
  ngx_resolve_name_done(resolver);
}

inline void send(ngx_pool_t* pool, ngx_log_t* log, std::string_view url) {
  auto u = static_cast<ngx_url_t*>(ngx_palloc(pool, sizeof(ngx_url_t)));
  /*u->url = nginx::to_ngx_str(url);*/
  u->url = ngx_string("localhost:8126");
  if (ngx_parse_url(pool, u) != NGX_OK) {
    // TODO: log error from u->err;
    return;
  }

  ngx_addr_t* addr = u->addrs;
  auto peer_conn = static_cast<ngx_peer_connection_t*>(
      ngx_palloc(pool, sizeof(ngx_peer_connection_t)));
  peer_conn->type = SOCK_STREAM;
  peer_conn->sockaddr = addr->sockaddr;
  peer_conn->socklen = addr->socklen;
  peer_conn->name = &addr->name;
  peer_conn->get = ngx_event_get_peer;
  peer_conn->log = log;
  peer_conn->log_error = NGX_ERROR_ERR;

  auto rc = ngx_event_connect_peer(peer_conn);
  if (rc == NGX_ERROR) {
    // TODO
    return;
  }

  if (rc == NGX_BUSY || rc == NGX_DECLINED) {
    return;
  }

  auto ctx = static_cast<ClientCtx*>(ngx_palloc(pool, sizeof(ClientCtx)));
  peer_conn->connection->data = static_cast<void*>(ctx);

  nghttp2_session_callbacks* callbacks;
  nghttp2_session_callbacks_new(&callbacks);
  nghttp2_session_callbacks_set_send_callback2(callbacks, send_callback);
  /*nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,*/
  /*                                                     on_frame_recv_callback);*/
  /*nghttp2_session_callbacks_set_on_data_chunk_recv_callback(*/
  /*    callbacks, on_data_chunk_recv_callback);*/
  /*nghttp2_session_callbacks_set_on_stream_close_callback(*/
  /*    callbacks, on_stream_close_callback);*/
  nghttp2_session_callbacks_set_on_header_callback(callbacks,
                                                   on_header_callback);
  /*nghttp2_session_callbacks_set_on_begin_headers_callback(*/
  /*    callbacks, on_begin_headers_callback);*/

  nghttp2_session_client_new(&ctx->session, callbacks, nullptr);

  nghttp2_session_callbacks_del(callbacks);

  nghttp2_nv hdrs[] = {MAKE_NV2(":method", "POST"), MAKE_NV2(":scheme", "http"),
                       MAKE_NV2(":authority", "localhost:8126"),
                       MAKE_NV2(":path", "/")};

  int32_t stream_id = nghttp2_submit_request2(ctx->session, NULL, hdrs,
                                              ARRLEN(hdrs), NULL, nullptr);
  if (stream_id < 0) {
    // TODO: Handle error
    /*errx(1, "Could not submit HTTP request: %s",
     * nghttp2_strerror(stream_id));*/
    return;
  }

  ctx->stream_id = stream_id;

  peer_conn->connection->read->handler = conn_read_handler;
  peer_conn->connection->write->handler = conn_write_handler;
  if (rc == NGX_OK) {
    conn_write_handler(peer_conn->connection->write);
  }
}

inline void conn_read_handler(ngx_event_t* event) {
  auto conn = static_cast<ngx_connection_t*>(event->data);

  auto ctx = static_cast<ClientCtx*>(conn->data);

  u_char buffer[1024];
  size_t n_recv = ngx_recv(conn, buffer, 1024);
  if (n_recv > 0) {
    nghttp2_ssize read_len =
        nghttp2_session_mem_recv2(ctx->session, buffer, 1024);
    if (read_len < 0) {
      // TODO: Better handling
      /*warnx("Fatal error: %s", nghttp2_strerror((int)readlen));*/
      /*delete_http2_session_data(session_data);*/
      return;
    }
  }

  if (n_recv == NGX_AGAIN) {
    if (ngx_handle_read_event(event, 0) != NGX_OK) {
      // TODO: Handle error
      return;
    }
  }
}

inline void conn_write_handler(ngx_event_t* event) {
  auto conn = static_cast<ngx_connection_t*>(event->data);

  auto ctx = static_cast<ClientCtx*>(conn->data);

  const uint8_t* buffer;
  auto size = nghttp2_session_mem_send2(ctx->session, &buffer);

  while (size > 0) {
    size_t n_sent = ngx_send(conn, (u_char*)buffer, size);
    if (n_sent == NGX_ERROR) {
      // TODO
      return;
    }

    size = nghttp2_session_mem_send2(ctx->session, &buffer);
  }

  event->handler = conn_noop_handler;
  if (event->timer_set) {
    ngx_del_timer(event);
  }

  if (ngx_handle_write_event(event, 0) != NGX_OK) {
    // TODO: Handle error
  }
}

inline void conn_noop_handler(ngx_event_t*) {}

}  // namespace http
}  // namespace common
}  // namespace datadog
