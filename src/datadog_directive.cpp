#include "datadog_directive.h"

#include <algorithm>
#include <cctype>
#include <datadog/json.hpp>
#include <istream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "datadog_conf.h"
#include "datadog_conf_handler.h"
#include "datadog_variable.h"
#include "dd.h"
#include "defer.h"
#include "log_conf.h"
#include "ngx_http_datadog_module.h"
#include "ngx_logger.h"
#include "ngx_script.h"
#include "string_util.h"
#include "tracing_library.h"

namespace datadog {
namespace nginx {
namespace {

auto command_source_location(const ngx_command_t *command, const ngx_conf_t *conf) {
  return conf_directive_source_location_t{.file_name = conf->conf_file->file.name,
                                          .line = conf->conf_file->line,
                                          .directive_name = command->name};
}

// An empty configuration instructs the member functions of `TracingLibrary` to
// substitute a default configuration instead of interpreting the string as a
// JSON encoded configuration.
const std::string_view TRACER_CONF_DEFAULT;

// Mark the place in the specified `conf` (at the current `command`) where
// the Datadog tracer's propagation styles were decided. This might happen
// explicitly when the `datadog_propagation_styles` configuration directive is
// encountered, or implicitly if a header-injecting directive is encountered
// first (e.g. `proxy_pass`, `grpc_pass`, `fastcgi_pass`).
// The purpose of locking the styles is to detect when
// `datadog_propagation_styles` occurs after a header-injecting directive.
char *lock_propagation_styles(const ngx_command_t *command, ngx_conf_t *conf) {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(conf, ngx_http_datadog_module));

  // The only way that `main_conf` could be `nullptr` is if there's no `http`
  // block in the nginx configuration.  In that case, this function
  // (`lock_propagation_styles`) would never get called, because it's called
  // only from configuration directives that live inside the `http` block.
  assert(main_conf != nullptr);

  // We need the propagation HTTP header names, below. But then they cannot be
  // changed by a subsequent `datadog_propagation_styles` directive.
  main_conf->are_propagation_styles_locked = true;
  main_conf->propagation_styles_source_location = command_source_location(command, conf);

  // In order for span context propagation to work, the names of the HTTP
  // headers added to requests need to be known ahead of time.
  NgxLogger logger;
  auto maybe_headers =
      TracingLibrary::propagation_header_names(main_conf->propagation_styles, logger);
  if (auto *error = maybe_headers.if_error()) {
    logger.log_error(*error);
    return static_cast<char *>(NGX_CONF_ERROR);
  }
  main_conf->span_context_keys = std::move(*maybe_headers);

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
// (See the definition of lock_propagation_styles).
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

  if (!main_conf->are_propagation_styles_locked) {
    if (auto rcode = lock_propagation_styles(command, cf)) {
      return rcode;
    }
  }
  // For each propagation header (from `span_context_keys`), add a
  // "proxy_set_header ...;" directive to the configuration, and then process
  // the injected directive by calling `datadog_conf_handler`.
  const auto &keys = main_conf->span_context_keys;

  auto old_args = cf->args;

  ngx_str_t args[] = {ngx_string("proxy_set_header"), ngx_str_t(), ngx_str_t()};
  ngx_array_t args_array;
  args_array.elts = static_cast<void *>(&args);
  args_array.nelts = sizeof args / sizeof args[0];

  cf->args = &args_array;
  const auto guard = defer([&]() { cf->args = old_args; });

  for (const std::string_view key : keys) {
    args[1] =
        ngx_str_t{key.size(), reinterpret_cast<unsigned char *>(const_cast<char *>(key.data()))};
    args[2] = make_propagation_header_variable(cf->pool, key);
    auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
    if (rcode != NGX_OK) {
      return static_cast<char *>(NGX_CONF_ERROR);
    }
  }
  return static_cast<char *>(NGX_CONF_OK);
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "datadog_propagate_context failed: %s", e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *warn_deprecated_command(ngx_conf_t *cf, ngx_command_t */*command*/, void */*conf*/) noexcept {
  const auto elements = static_cast<ngx_str_t *>(cf->args->elts);
  assert(cf->args->nelts >= 1);

  ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                "Directive \"%V\" is deprecated and can be removed since v1.0.15.",
                &elements[0]);

  return NGX_OK;
}

// Hijack proxy directive for tagging, then dispatch to the real handler
// for the specified `command`.
char *set_proxy_directive(ngx_conf_t *cf, ngx_command_t *command, void */* conf */) noexcept try {
  // First, call the handler of the actual command.
  // Be sure to skip this module, so we don't call ourself.
  const ngx_int_t rcode = datadog_conf_handler({.conf = cf, .skip_this_module = true});
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // Set the name of the proxy directive associated with this location.
  if (const auto loc_conf = static_cast<datadog_loc_conf_t *>(
          ngx_http_conf_get_module_loc_conf(cf, ngx_http_datadog_module))) {
    loc_conf->proxy_directive = command->name;
  }

  return NGX_OK;
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "Datadog-wrapped configuration directive %V failed: %s",
                command->name, e.what());
  return static_cast<char *>(NGX_CONF_ERROR);
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

char *json_config_deprecated(ngx_conf_t *cf, ngx_command_t *command, void * /*conf*/) noexcept {
  const auto location = command_source_location(command, cf);
  ngx_log_error(
      NGX_LOG_ERR, cf->log, 0,
      "The datadog { ... } block directive is no longer supported. Use the specific datadog_* "
      "directives instead, or use DD_TRACE_* environment variables.  "
      "Error occurred at \"%V\" in %V:%d",
      &location.directive_name, &location.file_name, location.line);
  return static_cast<char *>(NGX_CONF_ERROR);
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
                "The \"%V\" directive is no longer necessary.  Use the separate datadog_* "
                "directives to configure tracing.",
                &command->name);
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *set_datadog_sample_rate(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  const auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);

  conf_directive_source_location_t directive = command_source_location(command, cf);

  auto values = static_cast<ngx_str_t *>(cf->args->elts);
  // values[0] is the command name, "datadog_sample_rate".
  // The other elements are the arguments: either one or two of them.
  //
  //     datadog_sample_rate <rate> [on | off];
  std::string rate_str;
  rate_str += str(values[1]);
  ngx_str_t condition_pattern;
  if (cf->args->nelts == 3) {
    condition_pattern = values[2];
  } else {
    condition_pattern = ngx_string("on");
  }

  // Parse a float between 0.0 and 1.0 from the first argument (`rate_str`).
  double rate_float;
  try {
    std::size_t end_index;
    rate_float = std::stod(rate_str, &end_index);
    // `end_index` might not be the end of the input, e.g. if the argument were
    // "12monkeys".  That's an error.
    if (end_index != rate_str.size()) {
      ngx_log_error(
          NGX_LOG_ERR, cf->log, 0,
          "Invalid argument \"%V\" to %V directive at %V:%d.  Expected a real number between 0.0 "
          "and 1.0, but the provided argument has unparsed trailing characters.",
          &values[1], &directive.directive_name, &directive.file_name, directive.line);
      return static_cast<char *>(NGX_CONF_ERROR);
    }
    if (!(rate_float >= 0.0 && rate_float <= 1.0)) {
      throw std::out_of_range("");  // error message is in the `catch` handler
    }
  } catch (const std::invalid_argument &) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Invalid argument \"%V\" to %V directive at %V:%d.  Expected a real number "
                  "between 0.0 and 1.0, but the provided argument is not a number.",
                  &values[1], &directive.directive_name, &directive.file_name, directive.line);
    return static_cast<char *>(NGX_CONF_ERROR);
  } catch (const std::out_of_range &) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Invalid argument \"%V\" to %V directive at %V:%d.  Expected a real number "
                  "between 0.0 and 1.0, but the provided argument is out of range.",
                  &values[1], &directive.directive_name, &directive.file_name, directive.line);
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // Compile the pattern that evaluates to either "on" or "off" depending on
  // whether the specified sample rate should apply to the current request.
  NgxScript condition_script;
  if (condition_script.compile(cf, condition_pattern) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Invalid argument \"%V\" to %V directive at %V:%d.  Expected an expression that "
                  "will evaluate to \"on\" or \"off\".",
                  &condition_pattern, &directive.directive_name, &directive.file_name,
                  directive.line);
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // Add to the location configuration a `datadog_sample_rate_condition_t`
  // object corresponding to this `sample_rate` directive. This will allow us
  // to evaluate the condition (script) when a request comes through this
  // location.
  auto &rates = loc_conf->sample_rates;
  datadog_sample_rate_condition_t rate = {
      .condition = condition_script,
      .directive = directive,
      .same_line_index = 0,  // see below
  };
  if (!rates.empty() && rates.back().directive == rate.directive) {
    // Two "sample_rate" directives on the same line. Scandal.
    rate.same_line_index = rates.back().same_line_index + 1;
  }
  rates.push_back(rate);  // we use `rate` again below

  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));

  // The only way that `main_conf` could be `nullptr` is if there's no `http`
  // block in the nginx configuration.  In that case, this function would never
  // get called, because it's called only from configuration directives that
  // live inside the `http` block.
  assert(main_conf != nullptr);

  // Add a corresponding sampling rule to the main configuration.
  // This will end up in the tracer when it's instantiated in worker processes.
  sampling_rule_t rule;
  rule.depth = &loc_conf->depth;
  rule.rule.sample_rate = rate_float;
  rule.rule.tags.emplace(rate.tag_name(), rate.tag_value());
  main_conf->sampling_rules.push_back(std::move(rule));

  return static_cast<char *>(NGX_CONF_OK);
}

char *set_datadog_propagation_styles(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  const auto main_conf = static_cast<datadog_main_conf_t *>(conf);
  // If the propagation styles have already been configured, then either there
  // are two "datadog_propagation_styles" directives, or, more likely, another
  // directive like "proxy_pass" occurred earlier and default-configured the
  // propagation styles.  Print an error instructing the user to place
  // "datadog_propagation_styles" before any such directives.
  if (main_conf->are_propagation_styles_locked) {
    const auto &location = main_conf->propagation_styles_source_location;
    const char *qualifier = "";
    if (str(location.directive_name) != "datadog_propagation_styles") {
      qualifier = "default-";
    }
    ngx_log_error(
        NGX_LOG_ERR, cf->log, 0,
        "Datadog propagation styles are already configured.  They were %sconfigured by "
        "the call to \"%V\" at "
        "%V:%d.  Place the datadog_propagation_styles directive in the http block, before any "
        "proxy-related directives.",
        qualifier, &location.directive_name, &location.file_name, location.line);
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  const auto values = static_cast<ngx_str_t *>(cf->args->elts);
  // values[0] is the command name, "datadog_propagation_styles".
  // The other elements are the arguments: the names of the styles.
  //
  //     datadog_propagation_styles <style> [<styles> ...];
  const auto args = values + 1;
  const auto nargs = cf->args->nelts - 1;
  auto &styles = main_conf->propagation_styles;
  for (const ngx_str_t *arg = args; arg != args + nargs; ++arg) {
    auto maybe_style = dd::parse_propagation_style(str(*arg));
    if (!maybe_style) {
      const auto location = command_source_location(command, cf);
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "Invalid propagation style \"%V\". Acceptable values are \"Datadog\", \"B3\", "
                    "and \"tracecontext\". Error occurred at \"%V\" in %V:%d",
                    arg, &location.directive_name, &location.file_name, location.line);
      return static_cast<char *>(NGX_CONF_ERROR);
    }
    if (std::find(styles.begin(), styles.end(), *maybe_style) != styles.end()) {
      const auto location = command_source_location(command, cf);
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "Duplicate propagation style \"%V\". Error occurred at \"%V\" in %V:%d", arg,
                    &location.directive_name, &location.file_name, location.line);
      return static_cast<char *>(NGX_CONF_ERROR);
    }
    styles.push_back(*maybe_style);
  }

  return lock_propagation_styles(command, cf);
}

template <typename SetInDDConfig, typename GetFromFinalDDConfig>
static char *set_configured_value(
    ngx_conf_t *cf, ngx_command_t *command, void *conf,
    std::optional<configured_value_t> datadog_main_conf_t::*conf_member,
    SetInDDConfig &&set_in_dd_config, GetFromFinalDDConfig &&get_from_final_dd_config) {
  auto location = command_source_location(command, cf);

  auto *main_conf = static_cast<datadog_main_conf_t *>(conf);
  auto &field = main_conf->*conf_member;
  if (field) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Duplicate call to \"%V\". First call was at %V:%d. Duplicate call is at %V:%d.",
                  &field->location.directive_name, &field->location.file_name,
                  field->location.line, &location.file_name, location.line);
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  const auto values = static_cast<ngx_str_t *>(cf->args->elts);
  // values[0] is the command name, while values[1] is the single argument.
  const auto arg = str(values[1]);

  // Create a tracer config that contains the environment value. Then finalize
  // the config to obtain the final environment value, which might differ from
  // the original due to environment variables.
  dd::TracerConfig minimal_config;
  // A non-empty service name is required.
  minimal_config.defaults.service = "dummy";
  // Set the configuration property of interest.
  set_in_dd_config(minimal_config, arg);
  auto finalized_config = dd::finalize_config(minimal_config);
  if (auto *error = finalized_config.if_error()) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0, "Unable to check %V %V; [error code %d]: %s",
                  &values[0], &values[1], int(error->code), error->message.c_str());
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // Get the resulting configuration property of interest.
  const std::string &final_value = get_from_final_dd_config(*finalized_config);
  if (final_value != arg) {
    ngx_log_error(
        NGX_LOG_ERR, cf->log, 0,
        "\"%V %V;\" directive at  %V:%d is overriden to \"%s\" by an environment variable",
        &values[0], &values[1], &location.file_name, location.line, final_value.c_str());
  }

  field.emplace();
  field->value.assign(arg.data(), arg.size());
  field->location = std::move(location);

  return NGX_CONF_OK;
}

char *set_datadog_service_name(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  return set_configured_value(
      cf, command, conf, &datadog_main_conf_t::service_name,
      [](dd::TracerConfig &config, std::string_view service_name) {
        config.defaults.service = service_name;
      },
      [](const dd::FinalizedTracerConfig &config) { return config.defaults.service; });
}

char *set_datadog_environment(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  return set_configured_value(
      cf, command, conf, &datadog_main_conf_t::environment,
      [](dd::TracerConfig &config, std::string_view environment) {
        config.report_traces = false;  // don't bother with a collector (optimization)
        config.defaults.environment = environment;
      },
      [](const dd::FinalizedTracerConfig &config) { return config.defaults.environment; });
}

char *set_datadog_agent_url(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept {
  return set_configured_value(
      cf, command, conf, &datadog_main_conf_t::agent_url,
      [](dd::TracerConfig &config, std::string_view agent_url) { config.agent.url = agent_url; },
      [](const dd::FinalizedTracerConfig &config) {
        const auto &url = std::get<dd::FinalizedDatadogAgentConfig>(config.collector).url;
        return url.scheme + "://" + url.authority + url.path;
      });
}

}  // namespace nginx
}  // namespace datadog
