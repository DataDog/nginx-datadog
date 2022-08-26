#include "ngx_http_datadog_module.h"


#include <cstdlib>
#include <exception>
#include <iterator>
#include <string>
#include <utility>

#include "datadog_conf.h"
#include "datadog_conf_handler.h"
#include "datadog_directive.h"
#include "datadog_handler.h"
#include "datadog_variable.h"
#include "defer.h"
#include "load_tracer.h"
#include "log_conf.h"
#include "ot.h"
#include "string_util.h"
#include "string_view.h"
#include "tracing_library.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <stdlib.h>  // ::setenv
}

// clang-format off
static ngx_int_t datadog_module_init(ngx_conf_t *cf) noexcept;
static ngx_int_t datadog_init_worker(ngx_cycle_t *cycle) noexcept;
static ngx_int_t datadog_master_process_post_config(ngx_cycle_t *cycle) noexcept;
static void datadog_exit_worker(ngx_cycle_t *cycle) noexcept;
static void *create_datadog_main_conf(ngx_conf_t *conf) noexcept;
static void *create_datadog_loc_conf(ngx_conf_t *conf) noexcept;
static char *merge_datadog_loc_conf(ngx_conf_t *, void *parent, void *child) noexcept;
// clang-format on

using namespace datadog::nginx;

// Each "datadog_*" directive has a corresponding "opentracing_*" alias that
// logs a warning and then delegates to the "datadog_*" version, e.g.
// "opentracing_trace_locations" logs a warning and then calls
// "datadog_trace_locations".  The `ngx_command_t::type` bitmask of the two
// versions must match.  To ensure this, `DEFINE_COMMAND_WITH_OLD_ALIAS` is a
// macro that defines both commands at the same time.
#define DEFINE_COMMAND_WITH_OLD_ALIAS(NAME, OLD_NAME, TYPE, SET, CONF, OFFSET, POST) \
  {ngx_string(NAME), TYPE, SET, CONF, OFFSET, POST}, {                               \
    ngx_string(OLD_NAME), TYPE, delegate_to_datadog_directive_with_warning,          \
        NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr                                         \
  }

// Part of configuring a command is saying where the command is allowed to
// appear, e.g. in the `server` block, in a `location` block, etc.
// There are two sets of places Datadog commands can appear: either "anywhere,"
// or "anywhere but in the main section."  `anywhere` and `anywhere_but_main`
// are respective shorthands.
// clang-format off
static ngx_uint_t anywhere_but_main =
    NGX_HTTP_SRV_CONF  // an `http` block
  | NGX_HTTP_SIF_CONF    // an `if` block within an `http` block
  | NGX_HTTP_LOC_CONF  // a `location` block (within an `http` block)
  | NGX_HTTP_LIF_CONF;    // an `if` block within a `location` block (within an `http` block)

static ngx_uint_t anywhere =
    anywhere_but_main
  | NGX_HTTP_MAIN_CONF;  // the toplevel configuration, e.g. where modules are loaded

static ngx_command_t datadog_commands[] = {
    { ngx_string("opentracing"),
      anywhere | NGX_CONF_TAKE1,
      toggle_opentracing,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("datadog_enable"),
      anywhere | NGX_CONF_NOARGS,
      datadog_enable,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("datadog_disable"),
      anywhere | NGX_CONF_NOARGS,
      datadog_disable,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_trace_locations",
      "opentracing_trace_locations",
      anywhere | NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, enable_locations),
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_propagate_context",
      "opentracing_propagate_context",
      anywhere | NGX_CONF_NOARGS,
      propagate_datadog_context,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    { ngx_string("proxy_pass"),
      anywhere_but_main | NGX_CONF_TAKE1,
      hijack_proxy_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("fastcgi_pass"),
      anywhere_but_main | NGX_CONF_TAKE1,
      hijack_fastcgi_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("grpc_pass"),
      anywhere_but_main | NGX_CONF_TAKE1,
      hijack_grpc_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    // The configuration of this directive was copied from
    // ../nginx/src/http/modules/ngx_http_log_module.c.
    { ngx_string("access_log"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_HTTP_LMT_CONF|NGX_CONF_1MORE,
      hijack_access_log,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_fastcgi_propagate_context",
      "opentracing_fastcgi_propagate_context",
      anywhere | NGX_CONF_NOARGS,
      propagate_fastcgi_datadog_context,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_grpc_propagate_context",
      "opentracing_grpc_propagate_context",
      anywhere | NGX_CONF_NOARGS,
      propagate_grpc_datadog_context,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_operation_name",
      "opentracing_operation_name",
      anywhere | NGX_CONF_TAKE1,
      set_datadog_operation_name,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_location_operation_name",
      "opentracing_location_operation_name",
      anywhere | NGX_CONF_TAKE1,
      set_datadog_location_operation_name,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    { ngx_string("datadog_resource_name"),
      anywhere | NGX_CONF_TAKE1,
      set_datadog_resource_name,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("datadog_location_resource_name"),
      anywhere | NGX_CONF_TAKE1,
      set_datadog_location_resource_name,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_trust_incoming_span",
      "opentracing_trust_incoming_span",
      anywhere | NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, trust_incoming_span),
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_tag",
      "opentracing_tag",
      anywhere | NGX_CONF_TAKE2,
      set_datadog_tag,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    { ngx_string("datadog_load_tracer"),
       NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
       plugin_loading_deprecated,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("opentracing_load_tracer"),
       NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
       plugin_loading_deprecated,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("datadog"),
      NGX_MAIN_CONF | NGX_HTTP_MAIN_CONF | NGX_CONF_NOARGS | NGX_CONF_BLOCK,
      configure_tracer,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    ngx_null_command
};

static ngx_http_module_t datadog_module_ctx = {
    add_variables,                /* preconfiguration */
    datadog_module_init,      /* postconfiguration */
    create_datadog_main_conf, /* create main configuration */
    nullptr,                      /* init main configuration */
    nullptr,                      /* create server configuration */
    nullptr,                      /* merge server configuration */
    create_datadog_loc_conf,  /* create location configuration */
    merge_datadog_loc_conf    /* merge location configuration */
};

//------------------------------------------------------------------------------
// ngx_http_datadog_module
//------------------------------------------------------------------------------
ngx_module_t ngx_http_datadog_module = {
    NGX_MODULE_V1,
    &datadog_module_ctx, /* module context */
    datadog_commands,    /* module directives */
    NGX_HTTP_MODULE,         /* module type */
    nullptr,                 /* init master */
    datadog_master_process_post_config, /* init module */
    datadog_init_worker, /* init process */
    nullptr,                 /* init thread */
    nullptr,                 /* exit thread */
    datadog_exit_worker, /* exit process */
    nullptr,                 /* exit master */
    NGX_MODULE_V1_PADDING
};
// clang-format on

static ngx_int_t datadog_master_process_post_config(ngx_cycle_t *cycle) noexcept {
  // If tracing has not so far been configured, then give it a default
  // configuration.  This means that the nginx configuration did not use the
  // `datadog` directive, and did not use any overridden directives, such as
  // `proxy_pass`.
  const auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_cycle_get_module_main_conf(cycle, ngx_http_datadog_module));

  if (main_conf == nullptr) {
    // no config, no behavior
    return NGX_OK;
  }

  if (!main_conf->is_tracer_configured) {
    main_conf->is_tracer_configured = true;
    main_conf->tracer_conf = ngx_string("");  // default config
  }

  // Forward tracer-specific environment variables to worker processes.
  std::string name;
  for (const auto &env_var_name : TracingLibrary::environment_variable_names()) {
    name = env_var_name;
    if (const char *value = std::getenv(name.c_str())) {
      main_conf->environment_variables.push_back(
          environment_variable_t{.name = name, .value = value});
    }
  }

  return NGX_OK;
}

static ngx_int_t datadog_module_init(ngx_conf_t *cf) noexcept {
  auto core_main_config = static_cast<ngx_http_core_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module));
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));

  if (main_conf == nullptr) {
    // no config, no behavior
    return NGX_OK;
  }

  // Add handlers to create tracing data.
  auto handler = static_cast<ngx_http_handler_pt *>(
      ngx_array_push(&core_main_config->phases[NGX_HTTP_REWRITE_PHASE].handlers));
  if (handler == nullptr) return NGX_ERROR;
  *handler = on_enter_block;

  handler = static_cast<ngx_http_handler_pt *>(
      ngx_array_push(&core_main_config->phases[NGX_HTTP_LOG_PHASE].handlers));
  if (handler == nullptr) return NGX_ERROR;
  *handler = on_log_request;

  // Add default span tags.
  const auto tags = TracingLibrary::default_tags();
  if (tags.empty()) return NGX_OK;
  main_conf->tags = ngx_array_create(cf->pool, tags.size(), sizeof(datadog_tag_t));
  if (!main_conf->tags) return NGX_ERROR;
  for (const auto &tag : tags) {
    if (add_datadog_tag(cf, main_conf->tags, to_ngx_str(tag.first), to_ngx_str(tag.second)) !=
        NGX_CONF_OK) {
      return NGX_ERROR;
    }
  }

  return NGX_OK;
}

static ngx_int_t datadog_init_worker(ngx_cycle_t *cycle) noexcept try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_cycle_get_module_main_conf(cycle, ngx_http_datadog_module));
  if (!main_conf || !main_conf->is_tracer_configured) {
    return NGX_OK;
  }

  for (const auto &entry : main_conf->environment_variables) {
    const bool overwrite = false;
    ::setenv(entry.name.c_str(), entry.value.c_str(), overwrite);
  }

  std::shared_ptr<ot::Tracer> tracer = load_tracer(cycle->log, str(main_conf->tracer_conf));
  if (!tracer) {
    return NGX_ERROR;
  }

  ot::Tracer::InitGlobal(std::move(tracer));
  return NGX_OK;
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "failed to initialize tracer: %s", e.what());
  return NGX_ERROR;
}

static void datadog_exit_worker(ngx_cycle_t *cycle) noexcept {
  // If the `ot::Tracer` singleton has been set (in `datadog_init_worker`),
  // `Close` it and destroy it (technically, reduce its reference count).
  auto tracer = ot::Tracer::InitGlobal(nullptr);
  if (tracer != nullptr) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cycle->log, 0, "closing Datadog tracer");
    tracer->Close();
  }
}

//------------------------------------------------------------------------------
// create_datadog_main_conf
//------------------------------------------------------------------------------
static void *create_datadog_main_conf(ngx_conf_t *conf) noexcept {
  auto main_conf =
      static_cast<datadog_main_conf_t *>(ngx_pcalloc(conf->pool, sizeof(datadog_main_conf_t)));
  // Default initialize members.
  if (main_conf) {
    *main_conf = datadog_main_conf_t();
  }
  return main_conf;
}

static void examine_conf_args(ngx_conf_t *conf) noexcept {
  if (conf->args == nullptr) {
    return;
  }

  if (conf->args->nelts >= 1) {
    const auto str = static_cast<const ngx_str_t *>(conf->args->elts);
  }
}

static bool is_server_block_begin(const ngx_conf_t *conf) {
  return conf->args != nullptr && conf->args->nelts == 1 &&
         str(*static_cast<const ngx_str_t *>(conf->args->elts)) == "server";
}

//------------------------------------------------------------------------------
// create_datadog_loc_conf
//------------------------------------------------------------------------------
static void *create_datadog_loc_conf(ngx_conf_t *conf) noexcept {
  auto loc_conf =
      static_cast<datadog_loc_conf_t *>(ngx_pcalloc(conf->pool, sizeof(datadog_loc_conf_t)));
  if (!loc_conf) return nullptr;

  // Trace ID and span ID are automatically added to the access log by altering
  // the default log format to be one defined by this module.  We need to
  // inject `log_format` directives as soon as possible within the `http` block
  // of the configuration.  However, the only hook we have that's in an
  // appropriate place is in this "location" handler, but when the "location"
  // is actually a `server` block.  That allows us to insert `log_format`
  // directives _before_ the `server` block (and thus directly within the
  // `http` block).  It's _before_ because this function is not a configuration
  // block handler, is a configuration context constructor that is called
  // before the handler, so we're still currently "outside" the `server` block,
  // within the `http` block, which is the only place `log_format` is allowed.
  if (is_server_block_begin(conf)) {
    if (inject_datadog_log_formats(conf)) {
      return nullptr;  // error
    }
  }

  loc_conf->enable = NGX_CONF_UNSET;
  loc_conf->enable_locations = NGX_CONF_UNSET;
  loc_conf->trust_incoming_span = NGX_CONF_UNSET;

  return loc_conf;
}

namespace {

// Merge the specified `previous` script into the specified `current` script in
// the context of the specified `conf`.  If `current` does not have a value and
// `previous` does, then `previous` will be used.  If neither has a value, then
// the specified `default_pattern` will be used.  Return `NGX_CONF_OK` on
// success, or another value otherwise.
char *merge_script(ngx_conf_t *conf, NgxScript &previous, NgxScript &current,
                   ot::string_view default_pattern) {
  if (current.is_valid()) {
    return NGX_CONF_OK;
  }

  if (!previous.is_valid()) {
    const ngx_int_t rc = previous.compile(conf, to_ngx_str(default_pattern));
    if (rc != NGX_OK) {
      return (char *)NGX_CONF_ERROR;
    }
  }

  current = previous;
  return NGX_CONF_OK;
}

}  // namespace

//------------------------------------------------------------------------------
// merge_datadog_loc_conf
//------------------------------------------------------------------------------
static char *merge_datadog_loc_conf(ngx_conf_t *cf, void *parent, void *child) noexcept {
  auto prev = static_cast<datadog_loc_conf_t *>(parent);
  auto conf = static_cast<datadog_loc_conf_t *>(child);

  ngx_conf_merge_value(conf->enable, prev->enable, TracingLibrary::tracing_on_by_default());
  ngx_conf_merge_value(conf->enable_locations, prev->enable_locations,
                       TracingLibrary::trace_locations_by_default());

  if (const auto rc = merge_script(cf, prev->operation_name_script, conf->operation_name_script,
                                   TracingLibrary::default_request_operation_name_pattern())) {
    return rc;
  }
  if (const auto rc =
          merge_script(cf, prev->loc_operation_name_script, conf->loc_operation_name_script,
                       TracingLibrary::default_location_operation_name_pattern())) {
    return rc;
  }
  if (const auto rc = merge_script(cf, prev->resource_name_script, conf->resource_name_script,
                                   TracingLibrary::default_resource_name_pattern())) {
    return rc;
  }
  if (const auto rc =
          merge_script(cf, prev->loc_resource_name_script, conf->loc_resource_name_script,
                       TracingLibrary::default_resource_name_pattern())) {
    return rc;
  }
  if (const auto rc =
          merge_script(cf, prev->response_info_script, conf->response_info_script, "")) {
    return rc;
  }

  ngx_conf_merge_value(conf->trust_incoming_span, prev->trust_incoming_span, 1);

  // Create a new array that joins `prev->tags` and `conf->tags`. Since tags
  // are set consecutively and setting a tag with the same key as a previous
  // one overwrites it, we need to ensure that the tags in `conf->tags` come
  // after `prev->tags` so as to keep the value from the most specific
  // configuration.
  if (prev->tags && !conf->tags) {
    conf->tags = prev->tags;
  } else if (prev->tags && conf->tags) {
    std::unordered_map<std::string, datadog_tag_t> merged_tags;

    for (ngx_uint_t i = 0; i < prev->tags->nelts; i++) {
      datadog_tag_t *tag = &((datadog_tag_t *)prev->tags->elts)[i];
      std::string key;
      key.assign(reinterpret_cast<const char *>(tag->key_script.pattern_.data),
                 tag->key_script.pattern_.len);
      merged_tags[key] = *tag;
    }

    for (ngx_uint_t i = 0; i < conf->tags->nelts; i++) {
      datadog_tag_t *tag = &((datadog_tag_t *)conf->tags->elts)[i];
      std::string key;
      key.assign(reinterpret_cast<const char *>(tag->key_script.pattern_.data),
                 tag->key_script.pattern_.len);
      merged_tags[key] = *tag;
    }

    ngx_uint_t index = 0;
    for (const auto &kv : merged_tags) {
      if (index == conf->tags->nelts) {
        datadog_tag_t *tag = (datadog_tag_t *)ngx_array_push(conf->tags);

        if (!tag) {
          return (char *)NGX_CONF_ERROR;
        }

        *tag = kv.second;
      } else {
        datadog_tag_t *tags = (datadog_tag_t *)conf->tags->elts;
        tags[index] = kv.second;
      }

      index++;
    }
  }

  return NGX_CONF_OK;
}
