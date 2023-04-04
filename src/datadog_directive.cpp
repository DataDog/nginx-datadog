#include "datadog_directive.h"

#include <algorithm>
#include <cctype>
#include <datadog/json.hpp>
#include <istream>
#include <string>
#include <string_view>

#include "config_util.h"
#include "datadog_conf.h"
#include "datadog_conf_handler.h"
#include "datadog_variable.h"
#include "dd.h"
#include "defer.h"
#include "discover_span_context_keys.h"
#include "log_conf.h"
#include "ngx_filebuf.h"
#include "ngx_http_datadog_module.h"
#include "ngx_script.h"
#include "string_util.h"
#include "tracing_library.h"

namespace datadog {
namespace nginx {
namespace {

// Dispatch to the "real" handler for the specified `command`, and then invoke
// the specified `inject_propagation_commands` with the specified `cf`,
// `command`, and `conf`.  `inject_propagation_commands` is intended to do the
// Datadog-specific work associated with the hijacked `command`, e.g. insert
// `proxy_set_header` directives into the current configuration context.
// Return the value returned by `inject_propagation_commands`, or return
// `NGX_CONF_ERROR` if an error occurs.
char *hijack_pass_directive(char *(*inject_propagation_commands)(ngx_conf_t *cf,
                                                                 ngx_command_t *cmd, void *conf),
                            ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept try {
  // First, call the handler of the actual command that we're hijacking, e.g.
  // "proxy_pass".  Be sure to skip this module, so we don't call ourself.
  const ngx_int_t rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // Set the name of the proxy directive associated with this location.
  if (const auto loc_conf = static_cast<datadog_loc_conf_t *>(
          ngx_http_conf_get_module_loc_conf(cf, ngx_http_datadog_module))) {
    loc_conf->proxy_directive = command->name;
  }

  // Second, call the Datadog-specific handler that sets up context
  // propagation, e.g. `propagate_datadog_context`.
  return inject_propagation_commands(cf, command, conf);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "Datadog-wrapped configuration directive %V failed: %s",
                command->name, e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

// An empty configuration instructs the member functions of `TracingLibrary` to
// substitute a default configuration instead of interpreting the string as a
// JSON encoded configuration.
const std::string_view TRACER_CONF_DEFAULT;

// Mark the place in the specified `conf` (at the current `command`) where the
// Datadog tracer was configured with the specified `tracer_conf`.  This might
// happen explicitly when the `datadog {...}` configuration directive is
// encountered, or implicitly if certain other directives are encountered
// first.
char *set_tracer(const ngx_command_t *command, ngx_conf_t *conf, std::string_view tracer_conf) {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(conf, ngx_http_datadog_module));

  // The only way that `main_conf` could be `nullptr` is if there's no `http`
  // block in the nginx configuration.  In that case, this function
  // (`set_tracer`) would never get called, because it's called only from
  // configuration directives that live inside the `http` block.
  assert(main_conf != nullptr);

  main_conf->is_tracer_configured = true;
  main_conf->tracer_conf = to_ngx_str(conf->pool, tracer_conf);
  main_conf->tracer_conf_source_location =
      conf_directive_source_location_t{.file_name = conf->conf_file->file.name,
                                       .line = conf->conf_file->line,
                                       .directive_name = command->name};

  // In order for span context propagation to work, the names of the HTTP
  // headers added to requests need to be known ahead of time.
  // `discovery_span_context_keys` consults
  // `TracingLibrary::propagation_header_names`.
  main_conf->span_context_keys =
      discover_span_context_keys(conf->pool, conf->log, str(main_conf->tracer_conf));
  if (main_conf->span_context_keys == nullptr) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  return static_cast<char *>(NGX_CONF_OK);
}

}  // namespace

static char *set_script(ngx_conf_t *cf, ngx_command_t *command, NgxScript &script) noexcept {
  if (script.is_valid()) return const_cast<char *>("is duplicate");

  auto value = static_cast<ngx_str_t *>(cf->args->elts);
  auto pattern = &value[1];

  if (script.compile(cf, *pattern) != NGX_OK) return static_cast<char *>(NGX_CONF_ERROR);

  return static_cast<char *>(NGX_CONF_OK);
}

static ngx_str_t make_propagation_header_variable(ngx_pool_t *pool, std::string_view key) {
  auto prefix = TracingLibrary::propagation_header_variable_name_prefix();
  // result = "$" + prefix + key
  auto size = 1 + prefix.size() + key.size();
  auto data = static_cast<char *>(ngx_palloc(pool, size));
  if (data == nullptr) throw std::bad_alloc{};

  // result = "$" + prefix + key
  char *iter = data;
  *iter++ = '$';
  iter = std::copy(prefix.begin(), prefix.end(), iter);
  std::transform(key.begin(), key.end(), iter, header_transform_char);

  return {size, reinterpret_cast<unsigned char *>(data)};
}

// Converts keys to match the naming convention used by CGI parameters.
static ngx_str_t make_fastcgi_span_context_key(ngx_pool_t *pool, std::string_view key) {
  static const std::string_view http_prefix = "HTTP_";
  auto size = http_prefix.size() + key.size();
  auto data = static_cast<char *>(ngx_palloc(pool, size));
  if (data == nullptr) throw std::bad_alloc{};

  std::copy_n(http_prefix.data(), http_prefix.size(), data);

  std::transform(key.data(), key.data() + key.size(), data + http_prefix.size(), [](char c) {
    if (c == '-') return '_';
    return static_cast<char>(std::toupper(c));
  });

  return {size, reinterpret_cast<unsigned char *>(data)};
}

char *add_datadog_tag(ngx_conf_t *cf, ngx_array_t *tags, ngx_str_t key, ngx_str_t value) noexcept {
  if (!tags) return static_cast<char *>(NGX_CONF_ERROR);

  auto tag = static_cast<datadog_tag_t *>(ngx_array_push(tags));
  if (!tag) return static_cast<char *>(NGX_CONF_ERROR);

  ngx_memzero(tag, sizeof(datadog_tag_t));
  if (tag->key_script.compile(cf, key) != NGX_OK) return static_cast<char *>(NGX_CONF_ERROR);
  if (tag->value_script.compile(cf, value) != NGX_OK) return static_cast<char *>(NGX_CONF_ERROR);

  return static_cast<char *>(NGX_CONF_OK);
}

// Sets up headers to be added so that the active span context is propagated
// upstream when using ngx_http_proxy_module.
//
// The directive gets translated to the directives
//
//      proxy_set_header header_name0 $header_variable_key0
//      proxy_set_header header_name1 $header_variable_key1
//      ...
//      proxy_set_header header_nameN $header_variable_keyN
//
// where header_variable_keyN is a prefix variable that expands to the
// corresponding value of the active span context.
//
// The key value of proxy_set_header isn't allowed to be a variable, so the keys
// used for propagation need to be discovered before this directive is called.
// (See the definition of set_tracer).
//
// This approach was discussed here
//     http://mailman.nginx.org/pipermail/nginx-devel/2018-March/011008.html
char *propagate_datadog_context(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));

  // The only way that `main_conf` could be `nullptr` is if there's no `http`
  // block in the nginx configuration.  In that case, this function would never
  // get called, because it's called only from configuration directives that
  // live inside the `http` block.
  assert(main_conf != nullptr);

  if (!main_conf->is_tracer_configured) {
    if (auto rcode = set_tracer(command, cf, TRACER_CONF_DEFAULT)) {
      return rcode;
    }
  }
  if (main_conf->span_context_keys == nullptr) {
    return static_cast<char *>(NGX_CONF_OK);
  }
  // For each propagation header (from `span_context_keys`), add a
  // "proxy_set_header ...;" directive to the configuration and then process
  // the injected directive by calling `datadog_conf_handler`.
  auto keys = static_cast<std::string_view *>(main_conf->span_context_keys->elts);
  auto num_keys = static_cast<int>(main_conf->span_context_keys->nelts);

  auto old_args = cf->args;

  ngx_str_t args[] = {ngx_string("proxy_set_header"), ngx_str_t(), ngx_str_t()};
  ngx_array_t args_array;
  args_array.elts = static_cast<void *>(&args);
  args_array.nelts = sizeof args / sizeof args[0];

  cf->args = &args_array;
  const auto guard = defer([&]() { cf->args = old_args; });

  for (int key_index = 0; key_index < num_keys; ++key_index) {
    args[1] =
        ngx_str_t{keys[key_index].size(),
                  reinterpret_cast<unsigned char *>(const_cast<char *>(keys[key_index].data()))};
    args[2] = make_propagation_header_variable(cf->pool, keys[key_index]);
    auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
    if (rcode != NGX_OK) {
      return static_cast<char *>(NGX_CONF_ERROR);
    }
  }
  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "opentracing_propagate_context failed: %s", e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *hijack_proxy_pass(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  return hijack_pass_directive(&propagate_datadog_context, cf, command, conf);
}

char *delegate_to_datadog_directive_with_warning(ngx_conf_t *cf, ngx_command_t *command,
                                                 void *conf) noexcept {
  const auto elements = static_cast<ngx_str_t *>(cf->args->elts);
  assert(cf->args->nelts >= 1);

  const std::string_view deprecated_prefix{"opentracing_"};
  assert(starts_with(str(elements[0]), deprecated_prefix));

  // This `std::string` is the storage for a `ngx_str_t` used below.  This is
  // valid if we are certain that copies of / references to the `ngx_str_t` will
  // not outlive this `std::string` (they won't).
  std::string new_name{"datadog_"};
  const std::string_view suffix = slice(str(elements[0]), deprecated_prefix.size());
  new_name.append(suffix.data(), suffix.size());

  const ngx_str_t new_name_ngx = to_ngx_str(new_name);
  ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                "Backward compatibility with the \"%V\" configuration "
                "directive is deprecated.  "
                "Please use \"%V\" instead.  Occurred at %V:%d",
                &elements[0], &new_name_ngx, &cf->conf_file->file.name, cf->conf_file->line);

  // Rename the command (opentracing_*  →  datadog_*) and let
  // `datadog_conf_handler` dispatch to the appropriate handler.
  elements[0] = new_name_ngx;
  auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = false});
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  return static_cast<char *>(NGX_CONF_OK);
}

char *propagate_fastcgi_datadog_context(ngx_conf_t *cf, ngx_command_t *command,
                                        void *conf) noexcept try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));

  // The only way that `main_conf` could be `nullptr` is if there's no `http`
  // block in the nginx configuration.  In that case, this function would never
  // get called, because it's called only from configuration directives that
  // live inside the `http` block.
  assert(main_conf != nullptr);

  if (!main_conf->is_tracer_configured) {
    if (auto rcode = set_tracer(command, cf, TRACER_CONF_DEFAULT)) {
      return rcode;
    }
  }
  if (main_conf->span_context_keys == nullptr) {
    return static_cast<char *>(NGX_CONF_OK);
  }
  auto keys = static_cast<std::string_view *>(main_conf->span_context_keys->elts);
  auto num_keys = static_cast<int>(main_conf->span_context_keys->nelts);

  auto old_args = cf->args;

  ngx_str_t args[] = {ngx_string("fastcgi_param"), ngx_str_t(), ngx_str_t(),
                      ngx_string("if_not_empty")};
  ngx_array_t args_array;
  args_array.elts = static_cast<void *>(&args);
  args_array.nelts = sizeof args / sizeof args[0];

  cf->args = &args_array;
  const auto guard = defer([&]() { cf->args = old_args; });

  for (int key_index = 0; key_index < num_keys; ++key_index) {
    args[1] = make_fastcgi_span_context_key(cf->pool, keys[key_index]);
    args[2] = make_propagation_header_variable(cf->pool, keys[key_index]);
    auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
    if (rcode != NGX_OK) {
      return static_cast<char *>(NGX_CONF_ERROR);
    }
  }
  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "opentracing_fastcgi_propagate_context failed: %s",
                e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *hijack_fastcgi_pass(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  return hijack_pass_directive(&propagate_fastcgi_datadog_context, cf, command, conf);
}

char *propagate_grpc_datadog_context(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept
    try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));

  // The only way that `main_conf` could be `nullptr` is if there's no `http`
  // block in the nginx configuration.  In that case, this function would never
  // get called, because it's called only from configuration directives that
  // live inside the `http` block.
  assert(main_conf != nullptr);

  if (!main_conf->is_tracer_configured) {
    if (auto rcode = set_tracer(command, cf, TRACER_CONF_DEFAULT)) {
      return rcode;
    }
  }
  if (main_conf->span_context_keys == nullptr) {
    return static_cast<char *>(NGX_CONF_OK);
  }
  auto keys = static_cast<std::string_view *>(main_conf->span_context_keys->elts);
  auto num_keys = static_cast<int>(main_conf->span_context_keys->nelts);

  auto old_args = cf->args;

  ngx_str_t args[] = {ngx_string("grpc_set_header"), ngx_str_t(), ngx_str_t()};
  ngx_array_t args_array;
  args_array.elts = static_cast<void *>(&args);
  args_array.nelts = 3;

  cf->args = &args_array;
  const auto guard = defer([&]() { cf->args = old_args; });

  for (int key_index = 0; key_index < num_keys; ++key_index) {
    args[1] =
        ngx_str_t{keys[key_index].size(),
                  reinterpret_cast<unsigned char *>(const_cast<char *>(keys[key_index].data()))};
    args[2] = make_propagation_header_variable(cf->pool, keys[key_index]);
    auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
    if (rcode != NGX_OK) {
      return static_cast<char *>(NGX_CONF_ERROR);
    }
  }
  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "opentracing_grpc_propagate_context failed: %s",
                e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *hijack_grpc_pass(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  return hijack_pass_directive(&propagate_grpc_datadog_context, cf, command, conf);
}

char *hijack_access_log(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept try {
  // In case we need to change the `access_log` command's format to a
  // Datadog-specific default, first make sure that those formats are defined.
  ngx_int_t rcode = inject_datadog_log_formats(cf);
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // clang-format off
  // The [documentation][1] of `access_log` lists the following possibilities:
  //
  //     access_log path [format [buffer=size] [gzip[=level]] [flush=time] [if=condition]];
  //     access_log off;
  //
  // The case we modify here is where the user specifies a file path but no
  // format name.  Nginx defaults to the "combined" format, but we will instead
  // inject the "datadog_text" format, i.e.
  //
  //     access_log /path/to/access.log;
  //
  // becomes
  //
  //     access_log /path/to/access.log datadog_text;
  //
  // All other cases are left unmodified.
  //
  // [1]: http://nginx.org/en/docs/http/ngx_http_log_module.html#access_log
  // clang-format on

  const auto old_args = cf->args;
  const auto guard = defer([&]() { cf->args = old_args; });
  const auto old_elts = static_cast<const ngx_str_t *>(old_args->elts);
  const auto num_args = old_args->nelts;
  // `new_args` might temporarily replace `cf->args` (if we decide to inject a
  // format name).
  ngx_array_t new_args;
  ngx_str_t new_elts[] = {ngx_str_t(), ngx_str_t(), ngx_str_t()};
  if (num_args == 2 && str(old_elts[1]) != "off") {
    new_elts[0] = old_elts[0];
    new_elts[1] = old_elts[1];
    new_elts[2] = ngx_string("datadog_text");
    new_args.elts = new_elts;
    new_args.nelts = sizeof new_elts / sizeof new_elts[0];
    cf->args = &new_args;
  }

  // Call the handler of the actual command that we're hijacking ("access_log")
  // Be sure to skip this module, so we don't call ourself.
  rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }
  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "Datadog-wrapped configuration directive %V failed: %s",
                command->name, e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *set_datadog_tag(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  if (!loc_conf->tags) loc_conf->tags = ngx_array_create(cf->pool, 1, sizeof(datadog_tag_t));
  auto values = static_cast<ngx_str_t *>(cf->args->elts);
  return add_datadog_tag(cf, loc_conf->tags, values[1], values[2]);
}

char *configure_tracer(ngx_conf_t *cf, ngx_command_t *command, void * /*conf*/) noexcept {
  const ngx_uint_t starting_line = cf->conf_file->line;
  NgxFileBuf buffer(*cf->conf_file->buffer, cf->conf_file->file, "", &cf->conf_file->line);
  std::istream input(&buffer);
  std::string output;
  std::string error;
  scan_config_block(input, output, error, CommentPolicy::OMIT);
  if (!error.empty()) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Error reading \"datadog { ... }\" configuration block: %s", error.c_str());
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // Make sure that the contents of the "datadog { ... }" block are valid JSON.
  try {
    (void)nlohmann::json::parse(output);
  } catch (const nlohmann::detail::parse_error &json_error) {
    // `parse_error` knows the line number, but it's not accessible.
    // It can appear as part of the `.what()` message in a predictable way,
    // though, so we extract it if present.
    error = json_error.what();
    const std::string_view prefix = " at line ";
    const auto pos = error.find(prefix.data(), 0, prefix.size());
    if (pos == std::string::npos) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "Error reading \"datadog { ... }\" configuration block: %s", error.c_str());
      return static_cast<char *>(NGX_CONF_ERROR);
    }

    // "blah blah at line 4, column 18: blah blah"
    // → "blah blah at line 43, column 18: blah blah"
    std::size_t end_pos;
    const unsigned long line = std::stoul(error.substr(pos + prefix.size()), &end_pos);
    std::string modified_error;
    modified_error.append(error, 0, pos + prefix.size());
    modified_error += std::to_string(line + starting_line - 1);
    modified_error.append(error, pos + prefix.size() + end_pos, std::string::npos);
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Error reading \"datadog { ... }\" configuration block: %s",
                  modified_error.c_str());
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  const auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));

  // The only way that `main_conf` could be `nullptr` is if there's no `http`
  // block in the nginx configuration.  In that case, this function would never
  // get called, because it's called only from configuration directives that
  // live inside the `http` block.
  assert(main_conf != nullptr);

  // If the tracer has already been configured, then either there are two
  // "datadog { ... }" blocks, or, more likely, another directive like
  // "proxy_pass" occurred earlier and default-configured the tracer.  Print an
  // error instructing the user to place "datadog { ... }" before any such
  // directives.
  if (main_conf->is_tracer_configured) {
    const auto &location = main_conf->tracer_conf_source_location;
    const char *qualifier = "";
    if (str(location.directive_name) != "datadog") {
      qualifier = "default-";
    }
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Datadog tracing is already configured.  It was %sconfigured "
                  "by the call to \"%V\" at "
                  "%V:%d.  Place the datadog configuration directive before "
                  "any proxy-related directives.",
                  qualifier, &location.directive_name, &location.file_name, location.line);
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  return set_tracer(command, cf, output);
}

char *set_datadog_operation_name(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  return set_script(cf, command, loc_conf->operation_name_script);
}

char *set_datadog_location_operation_name(ngx_conf_t *cf, ngx_command_t *command,
                                          void *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  return set_script(cf, command, loc_conf->loc_operation_name_script);
}

char *set_datadog_resource_name(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  return set_script(cf, command, loc_conf->resource_name_script);
}

char *set_datadog_location_resource_name(ngx_conf_t *cf, ngx_command_t *command,
                                         void *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  return set_script(cf, command, loc_conf->loc_resource_name_script);
}

char *toggle_opentracing(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  const auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  const auto values = static_cast<const ngx_str_t *>(cf->args->elts);
  assert(cf->args->nelts == 2);

  std::string_view preferred;
  if (str(values[1]) == "on") {
    loc_conf->enable = true;
    preferred = "datadog_enable";
  } else if (str(values[1]) == "off") {
    loc_conf->enable = false;
    preferred = "datadog_disable";
  } else {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Invalid argument \"%V\" to %V directive.  Use \"on\" or \"off\". ", &values[1],
                  &command->name);
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // Warn the user to prefer the corresponding "datadog_{enable,disable}"
  // directive.
  const ngx_str_t preferred_str = to_ngx_str(preferred);
  ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                "Backward compatibility with the \"%V %V;\" configuration directive is "
                "deprecated.  Please use \"%V;\" instead.",
                &values[0], &values[1], &preferred_str);

  return static_cast<char *>(NGX_CONF_OK);
}

char *datadog_enable(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  const auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  loc_conf->enable = true;
  return static_cast<char *>(NGX_CONF_OK);
}

char *datadog_disable(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  const auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  loc_conf->enable = false;
  return static_cast<char *>(NGX_CONF_OK);
}

char *plugin_loading_deprecated(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "The \"%V\" directive is no longer necessary.  Use \"datadog { "
                "... }\" to "
                "configure tracing.",
                &command->name);
  return static_cast<char *>(NGX_CONF_ERROR);
}

}  // namespace nginx
}  // namespace datadog
