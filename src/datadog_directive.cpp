#include "datadog_directive.h"
#include "datadog_defer.h"
#include "ot.h"
#include "ngx_http_datadog_module.h"

#include "config_util.h"
#include "discover_span_context_keys.h"
#include "json.hpp"
#include "ngx_filebuf.h"
#include "ngx_script.h"
#include "datadog_conf.h"
#include "datadog_conf_handler.h"
#include "datadog_variable.h"
#include "utility.h"

#include <opentracing/string_view.h>

#include <algorithm>
#include <cctype>
#include <iostream> // TODO: no
#include <istream>
#include <string>

namespace datadog {
namespace nginx {
namespace {

// TODO: document
char *hijack_pass_directive(
    char *(*inject_propagation_commands)(ngx_conf_t *cf, ngx_command_t *cmd, void *conf),
    ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept try {
  // First, call the handler of the actual command that we're hijacking, e.g.
  // "proxy_pass".  Be sure to skip this module, so we don't call ourself.
  const ngx_int_t rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }
  // Second, call the Datadog-specific handler that sets up context
  // propagation, e.g. `propagate_datadog_context`.
  return inject_propagation_commands(cf, command, conf);
} catch (const std::exception &e) {
  // TODO: Will buffering be a problem?
  ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "hijacked %V failed: %s", command->name, e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

} // namespace

static char *set_script(ngx_conf_t *cf, ngx_command_t *command,
                        NgxScript &script) noexcept {
  if (script.is_valid()) return const_cast<char *>("is duplicate");

  auto value = static_cast<ngx_str_t *>(cf->args->elts);
  auto pattern = &value[1];

  if (script.compile(cf, *pattern) != NGX_OK)
    return static_cast<char *>(NGX_CONF_ERROR);

  return static_cast<char *>(NGX_CONF_OK);
}

static ngx_str_t make_span_context_value_variable(
    ngx_pool_t *pool, ot::string_view key) {
  auto size = 1 + opentracing_context_variable_name.size() + key.size();
  auto data = static_cast<char *>(ngx_palloc(pool, size));
  if (data == nullptr) throw std::bad_alloc{};

  int index = 0;
  data[index] = '$';
  index += 1;

  std::copy_n(opentracing_context_variable_name.data(),
              opentracing_context_variable_name.size(), data + index);
  index += opentracing_context_variable_name.size();

  std::transform(std::begin(key), std::end(key), data + index,
                 header_transform_char);

  return {size, reinterpret_cast<unsigned char *>(data)};
}

// Converts keys to match the naming convention used by CGI parameters.
static ngx_str_t make_fastcgi_span_context_key(ngx_pool_t *pool,
                                               ot::string_view key) {
  static const ot::string_view http_prefix = "HTTP_";
  auto size = http_prefix.size() + key.size();
  auto data = static_cast<char *>(ngx_palloc(pool, size));
  if (data == nullptr) throw std::bad_alloc{};

  std::copy_n(http_prefix.data(), http_prefix.size(), data);

  std::transform(key.data(), key.data() + key.size(), data + http_prefix.size(),
                 [](char c) {
                   if (c == '-') return '_';
                   return static_cast<char>(std::toupper(c));
                 });

  return {size, reinterpret_cast<unsigned char *>(data)};
}

char *add_datadog_tag(ngx_conf_t *cf, ngx_array_t *tags, ngx_str_t key,
                          ngx_str_t value) noexcept {
  if (!tags) return static_cast<char *>(NGX_CONF_ERROR);

  auto tag = static_cast<datadog_tag_t *>(ngx_array_push(tags));
  if (!tag) return static_cast<char *>(NGX_CONF_ERROR);

  ngx_memzero(tag, sizeof(datadog_tag_t));
  if (tag->key_script.compile(cf, key) != NGX_OK)
    return static_cast<char *>(NGX_CONF_ERROR);
  if (tag->value_script.compile(cf, value) != NGX_OK)
    return static_cast<char *>(NGX_CONF_ERROR);

  return static_cast<char *>(NGX_CONF_OK);
}

// Sets up headers to be added so that the active span context is propagated
// upstream when using ngx_http_proxy_module.
//
// The directive gets translated to the directives
//
//      proxy_set_header span_context_key0 $opentracing_context_key0
//      proxy_set_header span_context_key1 $opentracing_context_key1
//      ...
//      proxy_set_header span_context_keyN $opentracing_context_keyN
//
// where opentracing_context is a prefix variable that expands to the
// corresponding value of the active span context.
//
// The key value of proxy_set_header isn't allowed to be a variable, so the keys
// used for propagation need to be discovered before this directive is called.
// (See set_tracer below).
//
// This approach was dicussed here
//     http://mailman.nginx.org/pipermail/nginx-devel/2018-March/011008.html
char *propagate_datadog_context(ngx_conf_t *cf, ngx_command_t * /*command*/,
                                    void * conf) noexcept try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));
  if (!main_conf->tracer_library.data) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "opentracing_propagate_context before tracer loaded");
    return static_cast<char *>(NGX_CONF_ERROR);
  }
  if (main_conf->span_context_keys == nullptr) {
    return static_cast<char *>(NGX_CONF_OK);
  }
  auto keys = static_cast<ot::string_view *>(
      main_conf->span_context_keys->elts);
  auto num_keys = static_cast<int>(main_conf->span_context_keys->nelts);

  auto old_args = cf->args;

  ngx_str_t args[] = {ngx_string("proxy_set_header"), ngx_str_t(), ngx_str_t()};
  ngx_array_t args_array;
  args_array.elts = static_cast<void *>(&args);
  args_array.nelts = 3;

  cf->args = &args_array;
  const auto guard = defer([&]() { cf->args = old_args; });

  for (int key_index = 0; key_index < num_keys; ++key_index) {
    args[1] = ngx_str_t{keys[key_index].size(),
                        reinterpret_cast<unsigned char *>(
                            const_cast<char *>(keys[key_index].data()))};
    args[2] = make_span_context_value_variable(cf->pool, keys[key_index]);
    auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
    if (rcode != NGX_OK) {
      return static_cast<char *>(NGX_CONF_ERROR);
    }
  }
  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "opentracing_propagate_context failed: %s", e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *hijack_proxy_pass(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  return hijack_pass_directive(&propagate_datadog_context, cf, command, conf);
}

namespace {
bool starts_with(const ot::string_view& subject, const ot::string_view& prefix) {
  if (prefix.size() > subject.size()) {
    return false;
  }

  return std::mismatch(subject.begin(), subject.end(), prefix.begin()).second == prefix.end();
}

ot::string_view slice(const ot::string_view& text, int begin, int end) {
  if (begin < 0) {
    begin += text.size();
  }
  if (end < 0) {
    end += text.size();
  }
  return ot::string_view(text.data() + begin, end - begin);
}

ot::string_view slice(const ot::string_view& text, int begin) {
  return slice(text, begin, text.size());
}

} // namespace

char *delegate_to_datadog_directive_with_warning(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  const auto elements = static_cast<ngx_str_t*>(cf->args->elts); 
  assert(cf->args->nelts >= 1);
  
  const ot::string_view deprecated_prefix{"opentracing_"};
  assert(starts_with(str(elements[0]), deprecated_prefix));

  // This `std::string` is the storage for a `ngx_str_t` used below.  This is
  // valid if we are certain that copies of / references to the `ngx_str_t` will
  // not outlive this `std::string` (they won't).
  std::string new_name{"datadog_"};
  const ot::string_view suffix = slice(str(elements[0]), deprecated_prefix.size());
  new_name.append(suffix.data(), suffix.size());

  const ngx_str_t new_name_ngx = to_ngx_str(new_name);
  ngx_log_error(NGX_LOG_WARN, cf->log, 0, "Backward compatibility with the \"%V\" configuration directive is deprecated.  Please use \"%V\" instead.", &elements[0], &new_name_ngx);

  // Rename the command (opentracing_*  →  datadog_*) and let
  // `datadog_conf_handler` dispatch to the appropriate handler.
  elements[0] = new_name_ngx;
  auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = false});
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  return static_cast<char*>(NGX_CONF_OK);
}

char *propagate_fastcgi_datadog_context(ngx_conf_t *cf,
                                            ngx_command_t*,
                                            void *conf) noexcept try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));
  if (!main_conf->tracer_library.data) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "opentracing_fastcgi_propagate_context before tracer loaded");
    return static_cast<char *>(NGX_CONF_ERROR);
  }
  if (main_conf->span_context_keys == nullptr) {
    return static_cast<char *>(NGX_CONF_OK);
  }
  auto keys = static_cast<ot::string_view *>(
      main_conf->span_context_keys->elts);
  auto num_keys = static_cast<int>(main_conf->span_context_keys->nelts);

  auto old_args = cf->args;

  ngx_str_t args[] = {ngx_string("fastcgi_param"), ngx_str_t(), ngx_str_t()};
  ngx_array_t args_array;
  args_array.elts = static_cast<void *>(&args);
  args_array.nelts = 3;

  cf->args = &args_array;
  const auto guard = defer([&]() { cf->args = old_args; });

  for (int key_index = 0; key_index < num_keys; ++key_index) {
    args[1] = make_fastcgi_span_context_key(cf->pool, keys[key_index]);
    args[2] = make_span_context_value_variable(cf->pool, keys[key_index]);
    auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
    if (rcode != NGX_OK) {
      return static_cast<char *>(NGX_CONF_ERROR);
    }
  }
  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "opentracing_fastcgi_propagate_context failed: %s", e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *hijack_fastcgi_pass(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  return hijack_pass_directive(&propagate_fastcgi_datadog_context, cf, command, conf);
}

char *propagate_grpc_datadog_context(ngx_conf_t *cf, ngx_command_t *command,
                                         void *conf) noexcept try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));
  if (!main_conf->tracer_library.data) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "opentracing_grpc_propagate_context before tracer loaded");
    return static_cast<char *>(NGX_CONF_ERROR);
  }
  if (main_conf->span_context_keys == nullptr) {
    return static_cast<char *>(NGX_CONF_OK);
  }
  auto keys = static_cast<ot::string_view *>(
      main_conf->span_context_keys->elts);
  auto num_keys = static_cast<int>(main_conf->span_context_keys->nelts);

  auto old_args = cf->args;

  ngx_str_t args[] = {ngx_string("grpc_set_header"), ngx_str_t(), ngx_str_t()};
  ngx_array_t args_array;
  args_array.elts = static_cast<void *>(&args);
  args_array.nelts = 3;

  cf->args = &args_array;
  const auto guard = defer([&]() { cf->args = old_args; });

  for (int key_index = 0; key_index < num_keys; ++key_index) {
    args[1] = ngx_str_t{keys[key_index].size(),
                        reinterpret_cast<unsigned char *>(
                            const_cast<char *>(keys[key_index].data()))};
    args[2] = make_span_context_value_variable(cf->pool, keys[key_index]);
    auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
    if (rcode != NGX_OK) {
      return static_cast<char *>(NGX_CONF_ERROR);
    }
  }
  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "opentracing_grpc_propagate_context failed: %s", e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *hijack_grpc_pass(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  return hijack_pass_directive(&propagate_grpc_datadog_context, cf, command, conf);
}

char *set_datadog_tag(ngx_conf_t *cf, ngx_command_t *command,
                          void *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  if (!loc_conf->tags)
    loc_conf->tags = ngx_array_create(cf->pool, 1, sizeof(datadog_tag_t));
  auto values = static_cast<ngx_str_t *>(cf->args->elts);
  return add_datadog_tag(cf, loc_conf->tags, values[1], values[2]);
}

// TODO: no
char *configure(ngx_conf_t *cf, ngx_command_t *command,
                          void *conf) noexcept {
  std::cout << "Rejoice, for we have done the thing!\n" << std::flush;
  // TODO: Don't go directly to `line`. Use a temporary, and then overwrite later.
  // This way, when the JSON reader reports an error at line 3, we know it's at line n+3.
  /*
  NgxFileBuf buffer(*cf->conf_file->buffer, cf->conf_file->file, "{", &cf->conf_file->line);
  std::istream input(&buffer);
  // auto json = nlohmann::json::parse(input);
  nlohmann::json json;
  input >> json;
  std::cout << "Parsed the following JSON: " << json << '\n' << std::flush;
  */

  NgxFileBuf buffer(*cf->conf_file->buffer, cf->conf_file->file, "", &cf->conf_file->line);
  std::istream input(&buffer);
  std::string output;
  std::string error;
  scan_config_block(input, output, error, CommentPolicy::OMIT);
  std::cout << "error: " << error << '\n' << "output: " << output << '\n' << std::flush;
  
  auto json = nlohmann::json::parse(output);
  std::cout << "Parsed the following JSON: " << json << '\n' << std::flush;

  // std::string data;
  // std::cout << "result of read: " << bool(std::getline(input, data, '}')) << '\n' << std::flush;
  // std::cout << "data[:50]: " << data.substr(0, 50) << '\n' << std::flush;

  return NGX_CONF_OK;
}

char *set_datadog_operation_name(ngx_conf_t *cf, ngx_command_t *command,
                                     void *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  return set_script(cf, command, loc_conf->operation_name_script);
}

char *set_datadog_location_operation_name(ngx_conf_t *cf,
                                              ngx_command_t *command,
                                              void *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  return set_script(cf, command, loc_conf->loc_operation_name_script);
}

char *set_tracer(ngx_conf_t *cf, ngx_command_t *command,
                 void *conf) noexcept try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));
  auto values = static_cast<ngx_str_t *>(cf->args->elts);
  main_conf->tracer_library = values[1];
  main_conf->tracer_conf_file = values[2];

  // In order for span context propagation to work, the keys used by a tracer
  // need to be known ahead of time. OpenTracing-C++ doesn't currently have any
  // API for this, so we use an extended interface in `TracingLibrary`.
  main_conf->span_context_keys = discover_span_context_keys(
      cf->pool, cf->log, to_string(main_conf->tracer_conf_file).c_str());
  if (main_conf->span_context_keys == nullptr) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "set_tracer failed: %s", e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}
}  // namespace nginx
}  // namespace datadog
