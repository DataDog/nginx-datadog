#include "tracing/directives.h"

#include <cassert>

#include "datadog_conf_handler.h"
#include "ngx_http_datadog_module.h"

namespace datadog::nginx {
namespace {

auto command_source_location(const ngx_command_t *command,
                             const ngx_conf_t *conf) {
  return conf_directive_source_location_t{
      .file_name = conf->conf_file->file.name,
      .line = conf->conf_file->line,
      .directive_name = command->name};
}

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
  main_conf->propagation_styles_source_location =
      command_source_location(command, conf);

  return static_cast<char *>(NGX_CONF_OK);
}

}  // namespace

char *delegate_to_datadog_directive_with_warning(ngx_conf_t *cf,
                                                 ngx_command_t *command,
                                                 void *conf) noexcept {
  const auto elements = static_cast<ngx_str_t *>(cf->args->elts);
  assert(cf->args->nelts >= 1);

  const std::string_view deprecated_prefix{"opentracing_"};
  assert(starts_with(str(elements[0]), deprecated_prefix));

  // This `std::string` is the storage for a `ngx_str_t` used below.  This is
  // valid if we are certain that copies of / references to the `ngx_str_t` will
  // not outlive this `std::string` (they won't).
  std::string new_name{"datadog_"};
  const std::string_view suffix =
      slice(str(elements[0]), deprecated_prefix.size());
  new_name.append(suffix.data(), suffix.size());

  const ngx_str_t new_name_ngx = to_ngx_str(new_name);
  ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                "Backward compatibility with the \"%V\" configuration "
                "directive is deprecated.  "
                "Please use \"%V\" instead.  Occurred at %V:%d",
                &elements[0], &new_name_ngx, &cf->conf_file->file.name,
                cf->conf_file->line);

  // Rename the command (opentracing_*  →  datadog_*) and let
  // `datadog_conf_handler` dispatch to the appropriate handler.
  elements[0] = new_name_ngx;
  auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = false});
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  return static_cast<char *>(NGX_CONF_OK);
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

char *set_datadog_tag(ngx_conf_t *cf, ngx_command_t *command,
                      void *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  if (!loc_conf->tags)
    loc_conf->tags = ngx_array_create(cf->pool, 1, sizeof(datadog_tag_t));
  auto values = static_cast<ngx_str_t *>(cf->args->elts);
  return add_datadog_tag(cf, loc_conf->tags, values[1], values[2]);
}

char *json_config_deprecated(ngx_conf_t *cf, ngx_command_t *command,
                             void * /*conf*/) noexcept {
  const auto location = command_source_location(command, cf);
  ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "The datadog { ... } block directive is no longer supported. "
                "Use the specific datadog_* "
                "directives instead, or use DD_TRACE_* environment variables.  "
                "Error occurred at \"%V\" in %V:%d",
                &location.directive_name, &location.file_name, location.line);
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *toggle_opentracing(ngx_conf_t *cf, ngx_command_t *command,
                         void *conf) noexcept {
  const auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  const auto values = static_cast<const ngx_str_t *>(cf->args->elts);
  assert(cf->args->nelts == 2);

  if (str(values[1]) == "on") {
    loc_conf->enable_tracing = true;
  } else if (str(values[1]) == "off") {
    loc_conf->enable_tracing = false;
  } else {
    ngx_log_error(
        NGX_LOG_ERR, cf->log, 0,
        "Invalid argument \"%V\" to %V directive.  Use \"on\" or \"off\". ",
        &values[1], &command->name);
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // Warn the user to prefer the corresponding "datadog_tracing"
  // directive.
  const ngx_str_t preferred_str = to_ngx_str({"datadog_tracing"});
  ngx_log_error(
      NGX_LOG_WARN, cf->log, 0,
      "Backward compatibility with the \"%V %V;\" configuration directive is "
      "deprecated.  Please use \"%V;\" instead.",
      &values[0], &values[1], &preferred_str);

  return static_cast<char *>(NGX_CONF_OK);
}

char *plugin_loading_deprecated(ngx_conf_t *cf, ngx_command_t *command,
                                void *conf) noexcept {
  ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                "The \"%V\" directive is no longer necessary.  Use the "
                "separate datadog_* "
                "directives to configure tracing.",
                &command->name);
  return static_cast<char *>(NGX_CONF_ERROR);
}

char *set_datadog_sample_rate(ngx_conf_t *cf, ngx_command_t *command,
                              void *conf) noexcept {
  const auto loc_conf = static_cast<datadog_loc_conf_t *>(conf);

  conf_directive_source_location_t directive =
      command_source_location(command, cf);

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
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "Invalid argument \"%V\" to %V directive at %V:%d.  "
                    "Expected a real number between 0.0 "
                    "and 1.0, but the provided argument has unparsed trailing "
                    "characters.",
                    &values[1], &directive.directive_name, &directive.file_name,
                    directive.line);
      return static_cast<char *>(NGX_CONF_ERROR);
    }
    if (!(rate_float >= 0.0 && rate_float <= 1.0)) {
      throw std::out_of_range("");  // error message is in the `catch` handler
    }
  } catch (const std::invalid_argument &) {
    ngx_log_error(
        NGX_LOG_ERR, cf->log, 0,
        "Invalid argument \"%V\" to %V directive at %V:%d.  Expected a real "
        "number "
        "between 0.0 and 1.0, but the provided argument is not a number.",
        &values[1], &directive.directive_name, &directive.file_name,
        directive.line);
    return static_cast<char *>(NGX_CONF_ERROR);
  } catch (const std::out_of_range &) {
    ngx_log_error(
        NGX_LOG_ERR, cf->log, 0,
        "Invalid argument \"%V\" to %V directive at %V:%d.  Expected a real "
        "number "
        "between 0.0 and 1.0, but the provided argument is out of range.",
        &values[1], &directive.directive_name, &directive.file_name,
        directive.line);
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  // Compile the pattern that evaluates to either "on" or "off" depending on
  // whether the specified sample rate should apply to the current request.
  NgxScript condition_script;
  if (condition_script.compile(cf, condition_pattern) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Invalid argument \"%V\" to %V directive at %V:%d.  Expected "
                  "an expression that "
                  "will evaluate to \"on\" or \"off\".",
                  &condition_pattern, &directive.directive_name,
                  &directive.file_name, directive.line);
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

char *set_datadog_propagation_styles(ngx_conf_t *cf, ngx_command_t *command,
                                     void *conf) noexcept {
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
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                  "Datadog propagation styles are already configured.  They "
                  "were %sconfigured by "
                  "the call to \"%V\" at "
                  "%V:%d.  Place the datadog_propagation_styles directive in "
                  "the http block, before any "
                  "proxy-related directives.",
                  qualifier, &location.directive_name, &location.file_name,
                  location.line);
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
                    "Invalid propagation style \"%V\". Acceptable values are "
                    "\"Datadog\", \"B3\", "
                    "and \"tracecontext\". Error occurred at \"%V\" in %V:%d",
                    arg, &location.directive_name, &location.file_name,
                    location.line);
      return static_cast<char *>(NGX_CONF_ERROR);
    }
    if (std::find(styles.begin(), styles.end(), *maybe_style) != styles.end()) {
      const auto location = command_source_location(command, cf);
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "Duplicate propagation style \"%V\". Error occurred at "
                    "\"%V\" in %V:%d",
                    arg, &location.directive_name, &location.file_name,
                    location.line);
      return static_cast<char *>(NGX_CONF_ERROR);
    }
    styles.push_back(*maybe_style);
  }

  return lock_propagation_styles(command, cf);
}

char *set_datadog_agent_url(ngx_conf_t *cf, ngx_command_t *command,
                            void *conf) noexcept {
  assert(conf != nullptr);
  auto &main_conf = *static_cast<datadog_main_conf_t *>(conf);

  const auto values = static_cast<ngx_str_t *>(cf->args->elts);

  // values[0] is the command name, while values[1] is the single argument.
  const auto agent_url = to_string_view(values[1]);
  if (agent_url.empty()) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  main_conf.agent_url = std::string(agent_url);
  return NGX_OK;
}

char *warn_deprecated_command_1_2_0(ngx_conf_t *cf, ngx_command_t * /*command*/,
                                    void * /*conf*/) noexcept {
  const auto elements = static_cast<ngx_str_t *>(cf->args->elts);
  assert(cf->args->nelts >= 1);

  ngx_log_error(
      NGX_LOG_WARN, cf->log, 0,
      "Directive \"%V\" is deprecated and can be removed since v1.2.0.",
      &elements[0]);

  return NGX_OK;
}

}  // namespace datadog::nginx
