#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>

typedef unsigned char u_char;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef intptr_t ngx_int_t;
typedef uintptr_t ngx_uint_t;
typedef pid_t ngx_pid_t;
typedef ngx_uint_t ngx_msec_t;
typedef long ngx_atomic_int_t;
typedef unsigned long ngx_atomic_uint_t;
typedef int ngx_err_t;

typedef struct {
  size_t len;
  u_char* data;
} ngx_str_t;

typedef struct {
  unsigned len : 28;
  unsigned valid : 1;
  unsigned no_cacheable : 1;
  unsigned not_found : 1;
  unsigned escape : 1;
  u_char* data;
} ngx_variable_value_t;

struct ngx_log_t {
  ngx_uint_t log_level;
};

void ngx_log_error_core(ngx_uint_t level, ngx_log_t* log, ngx_err_t err,
                        const char* fmt, ...);

#define NGX_LOG_DEBUG 8
#define ngx_log_error(level, log, ...) \
  ngx_log_error_core(level, log, __VA_ARGS__)
#define ngx_log_debug(level, log, ...) \
  ngx_log_error_core(NGX_LOG_DEBUG, log, __VA_ARGS__)
#define ngx_log_debug1(level, log, err, fmt, arg1) \
  ngx_log_debug(level, log, err, fmt, arg1)

void valid(ngx_log_t* log) {
  ngx_str_t str{};
  ngx_variable_value_t variable{};
  u_char text[] = "text";

  ngx_log_error(1, log, 0,
                "%V %v %s %*s %O %P %T %M %z %uz %i %ui %d %ud %l %ul %D "
                "%uD %L %uL %A %uA %.3f %r %p %c %Z %N %%",
                &str, &variable, text, sizeof(text), text, off_t{}, ngx_pid_t{},
                time_t{}, ngx_msec_t{}, ssize_t{}, size_t{}, ngx_int_t{},
                ngx_uint_t{}, int{}, u_int{}, long{}, u_long{}, int32_t{},
                uint32_t{}, int64_t{}, uint64_t{}, ngx_atomic_int_t{},
                ngx_atomic_uint_t{}, double{}, rlim_t{}, &str, int{});

  ngx_log_debug1(1, log, 0, "debug %V", &str);
}

void invalid(ngx_log_t* log) {
  ngx_str_t str{};

  ngx_log_error(1, log, 0, "%V", str);
  ngx_log_error(1, log, 0, "%*s", int{}, "text");
  ngx_log_error(1, log, 0, "%ui", int{});
  ngx_log_error(1, log, 0, "%q", int{});
}
