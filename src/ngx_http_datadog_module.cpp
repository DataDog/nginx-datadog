#include "ngx_http_datadog_module.h"

#include <cassert>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

#include "datadog_conf.h"
#include "datadog_conf_handler.h"
#include "datadog_directive.h"
#include "datadog_handler.h"
#include "datadog_variable.h"
#include "dd.h"
#include "defer.h"
#include "global_tracer.h"
#include "ngx_logger.h"
#if defined(WITH_WAF)
#include "security/library.h"
#include "security/waf_remote_cfg.h"
#endif
#if defined(WITH_RUM)
#include "rum/config.h"
#endif
#include "string_util.h"
#include "tracing_library.h"
#include "version.h"

extern "C" {
#include <nginx.h>
#include <ngx_conf_file.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_log.h>
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

#ifndef DATADOG_RUM_DIRECTIVES
#define DATADOG_RUM_DIRECTIVES
#endif

// Each "datadog_*" directive has a corresponding "opentracing_*" alias that
// logs a warning and then delegates to the "datadog_*" version, e.g.
// "opentracing_trace_locations" logs a warning and then calls
// "datadog_trace_locations".  The `ngx_command_t::type` bitmask of the two
// versions must match.  To ensure this, `DEFINE_COMMAND_WITH_OLD_ALIAS` is a
// macro that defines both commands at the same time.
#define DEFINE_COMMAND_WITH_OLD_ALIAS(NAME, OLD_NAME, TYPE, SET, CONF, OFFSET, \
                                      POST)                                    \
  {ngx_string(NAME), TYPE, SET, CONF, OFFSET, POST}, {                         \
    ngx_string(OLD_NAME), TYPE, delegate_to_datadog_directive_with_warning,    \
        NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr                                   \
  }

#define DEFINE_DEPRECATED_COMMAND_1_2_0(NAME, TYPE)        \
  {                                                        \
    ngx_string(NAME), TYPE, warn_deprecated_command_1_2_0, \
        NGX_HTTP_LOC_CONF_OFFSET, 0, NULL                  \
  }

#define DEFINE_DEPRECATED_COMMAND_DATADOG_TRACING(NAME, TYPE)        \
  {                                                                  \
    ngx_string(NAME), TYPE, warn_deprecated_command_datadog_tracing, \
        NGX_HTTP_LOC_CONF_OFFSET, 0, NULL                            \
  }

// Part of configuring a command is saying where the command is allowed to
// appear, e.g. in the `server` block, in a `location` block, etc.
// There are two sets of places Datadog commands can appear: either "anywhere,"
// or "anywhere but in the main section."  `anywhere` and `anywhere_but_main`
// are respective shorthands.
// Also, this definition of "anywhere" excludes "if" blocks. "if" blocks
// do not behave as many users expect, and it might be a technical liability to
// support them. See
// <https://www.nginx.com/resources/wiki/start/topics/depth/ifisevil/>
// and <http://nginx.org/en/docs/http/ngx_http_rewrite_module.html#if>
// and <http://agentzh.blogspot.com/2011/03/how-nginx-location-if-works.html>.
//
// clang-format off
static ngx_uint_t anywhere_but_main =
    NGX_HTTP_SRV_CONF   // an `http` block
  | NGX_HTTP_LOC_CONF;  // a `location` block (within an `http` block)

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

    DEFINE_DEPRECATED_COMMAND_DATADOG_TRACING(
      "datadog_enable",
      anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_DATADOG_TRACING(
      "datadog_disable",
      anywhere | NGX_CONF_NOARGS
    ),

    { ngx_string("datadog_tracing"),
      anywhere | NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, enable_tracing),
      nullptr
    },

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_trace_locations",
      "opentracing_trace_locations",
      anywhere | NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, enable_locations),
      nullptr),

    DEFINE_DEPRECATED_COMMAND_1_2_0(
      "datadog_propagate_context",
      anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0(
      "opentracing_propagate_context",
      anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0(
      "opentracing_fastcgi_propagate_context",
      anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0(
      "datadog_fastcgi_propagate_context",
      anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0(
      "opentracing_grpc_propagate_context",
      anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0(
      "datadog_grpc_propagate_context",
      anywhere | NGX_CONF_NOARGS),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_operation_name",
      "opentracing_operation_name",
      anywhere | NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, operation_name_script),
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_location_operation_name",
      "opentracing_location_operation_name",
      anywhere | NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, loc_operation_name_script),
      nullptr),

    { ngx_string("datadog_resource_name"),
      anywhere | NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, resource_name_script),
      nullptr},

    { ngx_string("datadog_location_resource_name"),
      anywhere | NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, loc_resource_name_script),
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
      json_config_deprecated,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("datadog_sample_rate"),
      // NGX_CONF_TAKE12 means "take 1 or 2 args," not "take 12 args."
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE12,
      set_datadog_sample_rate,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("datadog_propagation_styles"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_1MORE,
      set_datadog_propagation_styles,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      nullptr},

    { ngx_string("datadog_service_name"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, service_name),
      nullptr},

    { ngx_string("datadog_environment"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, service_env),
      nullptr},

    { ngx_string("datadog_version"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, service_version),
      nullptr},

    { ngx_string("datadog_agent_url"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      set_datadog_agent_url,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      nullptr},

#ifdef WITH_WAF
    {
      ngx_string("datadog_waf_thread_pool_name"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      waf_thread_pool_name,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, waf_pool),
      NULL
    },

    {
      ngx_string("datadog_appsec_enabled"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(datadog_main_conf_t, appsec_enabled),
      nullptr,
    },

    {
      ngx_string("datadog_appsec_ruleset_file"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(datadog_main_conf_t, appsec_ruleset_file),
      nullptr,
    },

    {
      ngx_string("datadog_appsec_http_blocked_template_json"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(datadog_main_conf_t, appsec_http_blocked_template_json),
      nullptr,
    },

    {
      ngx_string("datadog_appsec_http_blocked_template_html"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(datadog_main_conf_t, appsec_http_blocked_template_html),
      nullptr,
    },

    {
      ngx_string("datadog_client_ip_header"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1, // TODO allow it more fine-grained
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(datadog_main_conf_t, custom_client_ip_header),
      nullptr,
    },

    {
      ngx_string("datadog_appsec_waf_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(datadog_main_conf_t, appsec_waf_timeout_ms),
      nullptr,
    },

    {
      ngx_string("datadog_appsec_obfuscation_key_regex"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(datadog_main_conf_t, appsec_obfuscation_key_regex),
      nullptr,
    },

    {
      ngx_string("datadog_appsec_obfuscation_value_regex"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(datadog_main_conf_t, appsec_obfuscation_value_regex),
      nullptr,
    },

    {
      ngx_string("datadog_appsec_max_saved_output_data"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(datadog_main_conf_t, appsec_max_saved_output_data),
      nullptr,
    },
#endif
    DATADOG_RUM_DIRECTIVES
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

static const char *phases_to_cstr(ngx_http_phases phase) {
  switch (phase) {
    case NGX_HTTP_POST_READ_PHASE:
      return "NGX_HTTP_POST_READ_PHASE";
    case NGX_HTTP_SERVER_REWRITE_PHASE:
      return "NGX_HTTP_SERVER_REWRITE_PHASE";
    case NGX_HTTP_FIND_CONFIG_PHASE:
      return "NGX_HTTP_FIND_CONFIG_PHASE";
    case NGX_HTTP_REWRITE_PHASE:
      return "NGX_HTTP_REWRITE_PHASE";
    case NGX_HTTP_POST_REWRITE_PHASE:
      return "NGX_HTTP_POST_REWRITE_PHASE";
    case NGX_HTTP_PREACCESS_PHASE:
      return "NGX_HTTP_PREACCESS_PHASE";
    case NGX_HTTP_ACCESS_PHASE:
      return "NGX_HTTP_ACCESS_PHASE";
    case NGX_HTTP_POST_ACCESS_PHASE:
      return "NGX_HTTP_POST_ACCESS_PHASE";
    case NGX_HTTP_PRECONTENT_PHASE:
      return "NGX_HTTP_PRECONTENT_PHASE";
    case NGX_HTTP_CONTENT_PHASE:
      return "NGX_HTTP_CONTENT_PHASE";
    case NGX_HTTP_LOG_PHASE:
      return "NGX_HTTP_LOG_PHASE";
  }
  return "UNKNOWN_PHASE";
}

template <typename F>
static int set_handler(ngx_log_t *log,
                       ngx_http_core_main_conf_t *core_main_conf,
                       ngx_http_phases phase, F callback) {
  auto handler = static_cast<ngx_http_handler_pt *>(
      ngx_array_push(&core_main_conf->phases[phase].handlers));
  if (handler == nullptr) return NGX_ERROR;
  *handler = callback;

  ngx_log_error(NGX_LOG_NOTICE, log, 0,
                "nginx-datadog: installed handler on %s phase",
                phases_to_cstr(phase));
  return NGX_OK;
}

// Configure nginx to set the environment variable as indicated by the
// specified `entry` in the context of the specified `cycle`.  `entry` is a
// string in one of the following forms:
//
// 1. "FOO"
// 2. "FOO=value"
//
// The environment variable name in this example is "FOO".  In the case of the
// first form, the value of the environment variable will be inherited from the
// parent process.  In the case of the second form, the value of the
// environment variable will be as specified after the equal sign.
//
// Note that `ngx_set_env` is adapted from the function of the same name in
// `nginx.c` within the nginx source code.
static void *ngx_set_env(std::string_view entry, ngx_cycle_t *cycle) {
  ngx_core_conf_t *ccf =
      (ngx_core_conf_t *)ngx_get_conf(cycle->conf_ctx, ngx_core_module);

  ngx_str_t *var;
  ngx_uint_t i;

  var = (ngx_str_t *)ngx_array_push(&ccf->env);
  if (var == NULL) {
    return NGX_CONF_ERROR;
  }

  const ngx_str_t entry_str = to_ngx_str(entry);
  *var = entry_str;

  for (i = 0; i < var->len; i++) {
    if (var->data[i] == '=') {
      var->len = i;
      return NGX_CONF_OK;
    }
  }

  return NGX_CONF_OK;
}

static ngx_int_t datadog_master_process_post_config(
    ngx_cycle_t *cycle) noexcept {
  ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "nginx-datadog status: enabled");
  ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "nginx-datadog version: %s (%s)",
                datadog_semver_nginx_mod, datadog_build_id_nginx_mod);
  ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "nginx-datadog features:");
  ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "- tracing: dd-trace-cpp@%s",
                datadog_version_tracer);

#ifdef WITH_RUM
  ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                "- rum-injection: inject-browser-sdk@%s",
                datadog_semver_rum_injector);
#endif

  // Forward tracer-specific environment variables to worker processes.
  for (const auto &env_var_name :
       TracingLibrary::environment_variable_names()) {
    if (const void *const error = ngx_set_env(env_var_name, cycle)) {
      return ngx_int_t(error);
    }
  }

  // If tracing has not so far been configured, then give it a default
  // configuration.  This means that the nginx configuration did not use the
  // `datadog` directive, and did not use any overridden directives, such as
  // `proxy_pass`.
  auto *const main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_cycle_get_module_main_conf(cycle, ngx_http_datadog_module));

  if (main_conf == nullptr) {
    // no config, no behavior
    return NGX_OK;
  }

#ifdef WITH_WAF
  ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                "- appsec: libddwaf@%s, waf_rules@%s", datadog_semver_libddwaf,
                datadog_semver_waf_rules);

  ngx_http_next_request_body_filter = ngx_http_top_request_body_filter;
  ngx_http_top_request_body_filter = request_body_filter;
#endif

  // Forward tracer-specific environment variables to worker processes.
  auto push_to_main_conf = [main_conf](std::string env_var_name) {
    if (const char *value = std::getenv(env_var_name.c_str())) {
      main_conf->environment_variables.push_back(
          environment_variable_t{.name = env_var_name, .value = value});
    }
  };
  for (const std::string_view &env_var_name :
       TracingLibrary::environment_variable_names()) {
    push_to_main_conf(std::string{env_var_name});
  }

#ifdef WITH_WAF
  for (const std::string_view &env_var_name :
       security::Library::environment_variable_names()) {
    push_to_main_conf(std::string{env_var_name});
  }
#endif

  return NGX_OK;
}

static ngx_int_t datadog_module_init(ngx_conf_t *cf) noexcept {
  ngx_http_next_header_filter = ngx_http_top_header_filter;
  ngx_http_top_header_filter = on_header_filter;

  ngx_http_next_output_body_filter = ngx_http_top_body_filter;
  ngx_http_top_body_filter = on_output_body_filter;

  auto core_main_config = static_cast<ngx_http_core_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module));
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));

  if (main_conf == nullptr) {
    // no config, no behavior
    return NGX_OK;
  }

  // Add handlers to create tracing data.
  if (set_handler(cf->log, core_main_config, NGX_HTTP_REWRITE_PHASE,
                  on_enter_block) != NGX_OK) {
    return NGX_ERROR;
  }

  if (set_handler(cf->log, core_main_config, NGX_HTTP_LOG_PHASE,
                  on_log_request) != NGX_OK) {
    return NGX_ERROR;
  }

#ifdef WITH_WAF
  if (set_handler(cf->log, core_main_config, NGX_HTTP_ACCESS_PHASE,
                  on_access) != NGX_OK) {
    return NGX_ERROR;
  }
#endif

  // Add default span tags.
  const auto tags = TracingLibrary::default_tags();
  if (tags.empty()) return NGX_OK;
  main_conf->tags =
      ngx_array_create(cf->pool, tags.size(), sizeof(datadog_tag_t));
  if (!main_conf->tags) return NGX_ERROR;
  for (const auto &tag : tags) {
    if (add_datadog_tag(cf, main_conf->tags, to_ngx_str(tag.first),
                        to_ngx_str(tag.second)) != NGX_CONF_OK) {
      return NGX_ERROR;
    }
  }

  return NGX_OK;
}

static ngx_int_t datadog_init_worker(ngx_cycle_t *cycle) noexcept try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_cycle_get_module_main_conf(cycle, ngx_http_datadog_module));
  if (!main_conf) {
    return NGX_OK;
  }

  std::shared_ptr<datadog::nginx::NgxLogger> logger =
      std::make_shared<NgxLogger>();

#ifdef WITH_WAF
  try {
    std::optional<security::ddwaf_owned_map> initial_waf_cfg =
        security::Library::initialize_security_library(*main_conf);
    if (initial_waf_cfg) {
      security::register_default_config(std::move(*initial_waf_cfg), logger);
    }
  } catch (const std::exception &e) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                  "Initialising security library failed: %s", e.what());
    return NGX_ERROR;
  }
#endif

  auto maybe_tracer = TracingLibrary::make_tracer(*main_conf, logger);
  if (auto *error = maybe_tracer.if_error()) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                  "Failed to construct tracer: [error code %d] %s",
                  int(error->code), error->message.c_str());
    return NGX_ERROR;
  }

  reset_global_tracer(std::move(*maybe_tracer));
  return NGX_OK;
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "failed to initialize tracer: %s",
                e.what());
  return NGX_ERROR;
}

static void datadog_exit_worker(ngx_cycle_t *cycle) noexcept {
  // If the `dd::Tracer` singleton has been set (in `datadog_init_worker`),
  // destroy it.
  reset_global_tracer();
}

// `register_destructor` allows us to have C++-allocated objects in the
// configuration. To avoid a leak, the C++ destructor must eventually be
// invoked. We do this in a memory pool cleanup handler.
// Return zero on success, or return a nonzero value if an error occurs.
template <typename Config>
static int register_destructor(ngx_pool_t *pool, Config *config) {
  ngx_pool_cleanup_t *cleanup = ngx_pool_cleanup_add(pool, 0);
  if (cleanup == nullptr) {
    return 1;
  }

  cleanup->data = config;
  cleanup->handler = static_cast<void (*)(void *)>([](void *data) {
    auto config = static_cast<Config *>(data);
    // `pool` is responsible for freeing the memory at `config`, but we want
    // to clean up any `std::string`, etc., not managed by `pool`. So, we call
    // the destructor here without `delete`, because the memory will later be
    // freed by `pool`.
    config->~Config();
  });

  return 0;
}

//------------------------------------------------------------------------------
// create_datadog_main_conf
//------------------------------------------------------------------------------
static void *create_datadog_main_conf(ngx_conf_t *conf) noexcept {
  void *memory = ngx_pcalloc(conf->pool, sizeof(datadog_main_conf_t));
  if (memory == nullptr) {
    return nullptr;  // error
  }

  auto main_conf = new (memory) datadog_main_conf_t{};
  if (register_destructor(conf->pool, main_conf)) {
    return nullptr;  // error
  }
  return main_conf;
}

//------------------------------------------------------------------------------
// create_datadog_loc_conf
//------------------------------------------------------------------------------
static void *create_datadog_loc_conf(ngx_conf_t *conf) noexcept {
  void *memory = ngx_pcalloc(conf->pool, sizeof(datadog_loc_conf_t));
  if (memory == nullptr) {
    return nullptr;  // error
  }

  auto loc_conf = new (memory) datadog_loc_conf_t{};
  if (register_destructor(conf->pool, loc_conf)) {
    return nullptr;  // error
  }

  return loc_conf;
}

static ngx_http_complex_value_t *make_default_complex_value(
    ngx_conf_t *cf, std::string_view default_value) {
  ngx_str_t value = to_ngx_str(default_value);
  auto *cv = (ngx_http_complex_value_t *)ngx_pcalloc(
      cf->pool, sizeof(ngx_http_complex_value_t));

  ngx_http_compile_complex_value_t ccv;
  ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

  ccv.cf = cf;
  ccv.value = &value;
  ccv.complex_value = cv;
  ccv.zero = 0;
  ccv.conf_prefix = 0;

  if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
    return nullptr;
  }

  return cv;
}

//------------------------------------------------------------------------------
// merge_datadog_loc_conf
//------------------------------------------------------------------------------
static char *merge_datadog_loc_conf(ngx_conf_t *cf, void *parent,
                                    void *child) noexcept {
  auto prev = static_cast<datadog_loc_conf_t *>(parent);
  auto conf = static_cast<datadog_loc_conf_t *>(child);

  conf->parent = prev;
  conf->depth = prev->depth + 1;

  ngx_conf_merge_value(conf->enable_tracing, prev->enable_tracing,
                       TracingLibrary::tracing_on_by_default());
  ngx_conf_merge_value(conf->enable_locations, prev->enable_locations,
                       TracingLibrary::trace_locations_by_default());
  ngx_conf_merge_ptr_value(conf->service_name, prev->service_name, nullptr);
  ngx_conf_merge_ptr_value(conf->service_env, prev->service_env, nullptr);
  ngx_conf_merge_ptr_value(conf->service_version, prev->service_version,
                           nullptr);
  ngx_conf_merge_ptr_value(
      conf->operation_name_script, prev->operation_name_script,
      make_default_complex_value(
          cf, TracingLibrary::default_request_operation_name_pattern()));
  ngx_conf_merge_ptr_value(
      conf->loc_operation_name_script, prev->loc_operation_name_script,
      make_default_complex_value(
          cf, TracingLibrary::default_location_operation_name_pattern()));
  ngx_conf_merge_ptr_value(
      conf->resource_name_script, prev->resource_name_script,
      make_default_complex_value(
          cf, TracingLibrary::default_resource_name_pattern()));
  ngx_conf_merge_ptr_value(
      conf->loc_resource_name_script, prev->loc_resource_name_script,
      make_default_complex_value(
          cf, TracingLibrary::default_resource_name_pattern()));
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

#ifdef WITH_WAF
  if (conf->waf_pool == nullptr) {
    conf->waf_pool = prev->waf_pool;
  }
#endif

#ifdef WITH_RUM
  datadog_rum_merge_loc_config(cf, prev, conf);
#endif

  return NGX_CONF_OK;
}
