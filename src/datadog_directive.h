#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

#include <string_view>

#include "string_util.h"

namespace datadog {
namespace nginx {

/// Represent an NGINX configuration directive.
/// This struct is there to leverage compile time generation of the module
/// directives. because `ngx_command_t` is not constexpr-friendly.
struct directive {
  /// Function pointer on the function that will be called when the directive
  /// will be processed.
  using setter_func = auto (*)(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
      -> char *;

  /// Name of the directive.
  std::string_view name;
  /// Type of directive.
  /// This value is a bitmask indicating the directive's applicable
  /// configuration contexts * (e.g., `NGX_HTTP_MAIN_CONF`, `NGX_HTTP_SRV_CONF`,
  /// etc.).
  ngx_uint_t type;
  /// Function pointer to the directive handler. This function is called when
  /// the directive is encountered during configuration parsing.
  setter_func callback;
  /// Configuration flag for the directive.
  ngx_uint_t conf;
  /// Offset for storing the directive's value in a configuration structure.
  ngx_uint_t offset;
  /// Additional post-processing data.
  void *post;

  ngx_command_t to_ngx_command() const {
    return ngx_command_t{to_ngx_str(name), type, callback, conf, offset, post};
  }
};

template <std::size_t N1, std::size_t... Ns>
constexpr auto merge_directives(const directive (&arr1)[N1],
                                const directive (&...arrs)[Ns]) {
  constexpr std::size_t total = N1 + (Ns + ...);

  std::array<ngx_command_t, total> result{};

  std::size_t offset = 0;  // Index to track insertion position
  for (std::size_t i = 0; i < N1; ++i) {
    result[offset++] = arr1[i].to_ngx_command();
  }

  (([&] {
     for (std::size_t i = 0; i < Ns; ++i) {
       result[offset++] = arrs[i].to_ngx_command();
     }
   }()),
   ...);

  return result;
}

template <typename... T>
constexpr auto generate_directives(const T &...directives) {
  return merge_directives(directives...);
}

#define IGNORE_COMMAND(NAME, TYPE) \
  { NAME, TYPE, silently_ignore_command, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr }

#define ALIAS_COMMAND(SRC_DIR, TARGET_DIR, TYPE)                    \
  {                                                                 \
    TARGET_DIR, TYPE, alias_directive, NGX_HTTP_LOC_CONF_OFFSET, 0, \
        (void *)SRC_DIR                                             \
  }

char *silently_ignore_command(ngx_conf_t *, ngx_command_t *, void *);

char *alias_directive(ngx_conf_t *cf, ngx_command_t *command,
                      void *conf) noexcept;

char *set_datadog_agent_url(ngx_conf_t *, ngx_command_t *, void *conf) noexcept;

char *warn_deprecated_command_datadog_tracing(ngx_conf_t *cf,
                                              ngx_command_t * /*command*/,
                                              void * /*conf*/) noexcept;

}  // namespace nginx
}  // namespace datadog
