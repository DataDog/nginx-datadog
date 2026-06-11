# nginx log format clang-tidy check

This directory contains an out-of-tree clang-tidy module with one check:

- `nginx-datadog-ngx-log-format`

The check validates literal format strings passed to `ngx_log_error`,
`ngx_log_debug`, `ngx_log_debug0` ... `ngx_log_debug8`, and direct
`ngx_log_error_core` calls.  It implements the format grammar used by nginx's
`ngx_vslprintf` in `src/core/ngx_string.c`.

Build and run it with:

```sh
BUILD_DIR=.build bin/nginx-log-format-tidy.sh
```

The build directory must contain `compile_commands.json` and generated nginx
headers.  If you want the numbered `ngx_log_debugN` macros to be visible to the
AST, configure nginx with debug support or use the runner script, which adds
`-DNGX_DEBUG=1` during analysis.

The plugin must be built with the same Clang major version as the `clang-tidy`
binary that loads it.  The runner uses `CLANG_TIDY` or `clang-tidy` from
`PATH`, and looks for the matching `llvm-config` next to that binary or via
`LLVM_CONFIG`/`LLVM_DIR`.

The checked argument types come from nginx's `ngx_vslprintf` parser:

| Specifier | Expected argument |
| --- | --- |
| `%V` | `ngx_str_t *` |
| `%v` | `ngx_variable_value_t *` |
| `%s`, `%xs`, `%Xs` | `u_char *` or another character pointer |
| `%*s` | `size_t`, then a character pointer |
| `%O` | `off_t` |
| `%P` | `ngx_pid_t` |
| `%T` | `time_t` |
| `%M` | `ngx_msec_t` |
| `%z`, `%uz` | `ssize_t`, `size_t` |
| `%i`, `%ui` | `ngx_int_t`, `ngx_uint_t` |
| `%d`, `%ud` | `int`, `u_int` |
| `%l`, `%ul` | `long`, `u_long` |
| `%D`, `%uD` | `int32_t`, `uint32_t` |
| `%L`, `%uL` | `int64_t`, `uint64_t` |
| `%A`, `%uA` | `ngx_atomic_int_t`, `ngx_atomic_uint_t` |
| `%f` | `double` |
| `%r` | `rlim_t` |
| `%p` | pointer |
| `%c` | `int` after default argument promotion |
| `%Z`, `%N`, `%%` | no argument |
