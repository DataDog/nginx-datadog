#include "load_tracer.h"
#include "ot.h"
#include "ngx_http_datadog_module.h"

#include "datadog_conf.h"
#include "datadog_directive.h"
#include "datadog_handler.h"
#include "datadog_variable.h"
#include "tracing_library.h"
#include "utility.h"

#include <opentracing/dynamic_load.h>

#include <cstdlib>
#include <exception>
#include <iostream> // TODO: no
#include <iterator>
#include <utility>

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
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

const std::pair<ngx_str_t, ngx_str_t> default_datadog_tags[] = {
    {ngx_string("component"), ngx_string("nginx")},
    {ngx_string("nginx.worker_pid"), ngx_string("$pid")},
    {ngx_string("peer.address"), ngx_string("$remote_addr:$remote_port")},
    {ngx_string("upstream.address"), ngx_string("$upstream_addr")},
    {ngx_string("http.method"), ngx_string("$request_method")},
    {ngx_string("http.url"), ngx_string("$scheme://$http_host$request_uri")},
    {ngx_string("http.host"), ngx_string("$http_host")}};

// Each "datadog_*" directive has a corresponding "opentracing_*" alias that
// logs a warning and then delegates to the "datadog_*" version, e.g.
// "opentracing_trace_locations" logs a warning and then calls
// "datadog_trace_locations".  The `ngx_command_t::type` bitmask of the two
// versions must match.  To ensure this, `DEFINE_COMMAND_WITH_OLD_ALIAS` is a
// macro that defines both commands at the same time.
#define DEFINE_COMMAND_WITH_OLD_ALIAS(NAME, OLD_NAME, TYPE, SET, CONF, OFFSET, POST) \
    { \
        ngx_string(NAME), \
        TYPE, \
        SET, \
        CONF, \
        OFFSET, \
        POST \
    }, \
    { \
        ngx_string(OLD_NAME), \
        TYPE, \
        delegate_to_datadog_directive_with_warning, \
        NGX_HTTP_LOC_CONF_OFFSET, \
        0, \
        nullptr \
    }

// clang-format off
static ngx_command_t datadog_commands[] = {

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "opentracing", 
      "datadog",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, enable),
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_trace_locations",
      "opentracing_trace_locations",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, enable_locations),
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_trace_locations",
      "opentracing_trace_locations",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      delegate_to_datadog_directive_with_warning,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),
      
    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_propagate_context",
      "opentracing_propagate_context",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
      propagate_datadog_context,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    // TODO: the other hijacked directives, as well
    { ngx_string("proxy_pass"),
      NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      hijack_proxy_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr},

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_fastcgi_propagate_context",
      "opentracing_fastcgi_propagate_context",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
      propagate_fastcgi_datadog_context,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_grpc_propagate_context",
      "opentracing_grpc_propagate_context",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
      propagate_grpc_datadog_context,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_operation_name",
      "opentracing_operation_name",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      set_datadog_operation_name,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_location_operation_name",
      "opentracing_location_operation_name",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      set_datadog_location_operation_name,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_trust_incoming_span",
      "opentracing_trust_incoming_span",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(datadog_loc_conf_t, trust_incoming_span),
      nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
      "datadog_tag",
      "opentracing_tag",
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE2,
      set_datadog_tag,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    // TODO: No need for a datadog version, and replace the opentracing version with an error.
     DEFINE_COMMAND_WITH_OLD_ALIAS(
       "datadog_load_tracer",
       "opentracing_load_tracer",
       NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
       set_tracer,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      nullptr),

    { ngx_string("datadog_configure"),
      NGX_MAIN_CONF | NGX_HTTP_MAIN_CONF | NGX_CONF_NOARGS | NGX_CONF_BLOCK,
      configure,
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
static void*
ngx_set_env(ot::string_view entry, ngx_cycle_t *cycle)
{
    ngx_core_conf_t *ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    ngx_str_t   *value, *var;
    ngx_uint_t   i;

    var = (ngx_str_t*) ngx_array_push(&ccf->env);
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

static ngx_int_t datadog_master_process_post_config(ngx_cycle_t *cycle) noexcept {
  // Forward tracer-specific environment variables to child processes (i.e.
  // workers).
  for (const auto& env_var_name : TracingLibrary::environment_variable_names()) {
    if (const void *const error = ngx_set_env(env_var_name, cycle)) {
      return ngx_int_t(error);
    }
  }

  return NGX_OK;
}

static ngx_int_t datadog_module_init(ngx_conf_t *cf) noexcept {
  auto core_main_config = static_cast<ngx_http_core_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module));
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_conf_get_module_main_conf(cf, ngx_http_datadog_module));

  // Add handlers to create tracing data.
  auto handler = static_cast<ngx_http_handler_pt *>(ngx_array_push(
      &core_main_config->phases[NGX_HTTP_REWRITE_PHASE].handlers));
  if (handler == nullptr) return NGX_ERROR;
  *handler = on_enter_block;

  handler = static_cast<ngx_http_handler_pt *>(
      ngx_array_push(&core_main_config->phases[NGX_HTTP_LOG_PHASE].handlers));
  if (handler == nullptr) return NGX_ERROR;
  *handler = on_log_request;

  // Add default span tags.
  const auto num_default_tags =
      sizeof(default_datadog_tags) / sizeof(default_datadog_tags[0]);
  if (num_default_tags == 0) return NGX_OK;
  main_conf->tags =
      ngx_array_create(cf->pool, num_default_tags, sizeof(datadog_tag_t));
  if (!main_conf->tags) return NGX_ERROR;
  for (const auto &tag : default_datadog_tags)
    if (add_datadog_tag(cf, main_conf->tags, tag.first, tag.second) !=
        NGX_CONF_OK)
      return NGX_ERROR;
  return NGX_OK;
}

static ngx_int_t datadog_init_worker(ngx_cycle_t *cycle) noexcept try {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_cycle_get_module_main_conf(cycle, ngx_http_datadog_module));
  if (!main_conf || !main_conf->tracer_library.data) {
    return NGX_OK;
  }

  ot::DynamicTracingLibraryHandle dummy_handle;
  std::shared_ptr<ot::Tracer> tracer;
  auto result = load_tracer(
      cycle->log, to_string(main_conf->tracer_library).data(),
      to_string(main_conf->tracer_conf_file).data(), dummy_handle, tracer);
  if (result != NGX_OK) {
    return result;
  }

  ot::Tracer::InitGlobal(std::move(tracer));
  return NGX_OK;
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "failed to initialize tracer: %s",
                e.what());
  return NGX_ERROR;
}

static void datadog_exit_worker(ngx_cycle_t *cycle) noexcept {
  // Close the global tracer if it's set and release the reference so as to
  // ensure that any dynamically loaded tracer is destructed before the library
  // handle is closed.
  auto tracer = ot::Tracer::InitGlobal(nullptr);
  if (tracer != nullptr) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cycle->log, 0,
                   "closing Datadog tracer");
    tracer->Close();
    tracer.reset();
  }
}

// TODO: hack hack
static void print_module_names(const ngx_cycle_t *cycle) noexcept {
  std::cout << "BEGIN print module names in " __FILE__ "\n";
  for (int i = 0; i < cycle->modules_n; ++i) {
    std::cout << "cycle has module: " << cycle->modules[i]->name << "\n";
  }
  std::cout << "END print module names\n";
}
// end TODO

//------------------------------------------------------------------------------
// create_datadog_main_conf
//------------------------------------------------------------------------------
static void *create_datadog_main_conf(ngx_conf_t *conf) noexcept {
  // TODO hack
  // print_module_names((const ngx_cycle_t*)ngx_cycle);
  // print_module_names(conf->cycle);
  // end TODO
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_pcalloc(conf->pool, sizeof(datadog_main_conf_t)));
  // Default initialize members.
  *main_conf = datadog_main_conf_t();
  if (!main_conf) return nullptr;
  return main_conf;
}

// TODO: hack hack
static void peek_conf_file(ngx_conf_t *conf) noexcept {
  const auto pos = reinterpret_cast<const char*>(conf->conf_file->buffer->pos);
  const auto last = reinterpret_cast<const char*>(conf->conf_file->buffer->last);
  const int max_length = 100;
  const auto end = std::min(last, pos + max_length);
  std::cout << "Looking ahead in config file:\n" << std::string(pos, end) << "\n";
}

static void examine_conf_args(ngx_conf_t *conf) noexcept {
  if (conf->args == nullptr) {
    std::cout << "conf args is null\n";
    return;
  }

  std::cout << "conf args has this many elements: " << conf->args->nelts << "\n";
  
  if (conf->args->nelts >= 1) {
    const auto str = static_cast<const ngx_str_t*>(conf->args->elts);
    std::cout << "Here's the first arg as a string: " << std::string(reinterpret_cast<const char*>(str->data), str->len) << "\n";
  }
}
// end TODO

//------------------------------------------------------------------------------
// create_datadog_loc_conf
//------------------------------------------------------------------------------
static void *create_datadog_loc_conf(ngx_conf_t *conf) noexcept {
  auto loc_conf = static_cast<datadog_loc_conf_t *>(
      ngx_pcalloc(conf->pool, sizeof(datadog_loc_conf_t)));
  if (!loc_conf) return nullptr;

  // TODO hack
  examine_conf_args(conf);
  peek_conf_file(conf);
  // end TODO

  loc_conf->enable = NGX_CONF_UNSET;
  loc_conf->enable_locations = NGX_CONF_UNSET;
  loc_conf->trust_incoming_span = NGX_CONF_UNSET;

  return loc_conf;
}

//------------------------------------------------------------------------------
// merge_datadog_loc_conf
//------------------------------------------------------------------------------
static char *merge_datadog_loc_conf(ngx_conf_t *, void *parent,
                                        void *child) noexcept {
  auto prev = static_cast<datadog_loc_conf_t *>(parent);
  auto conf = static_cast<datadog_loc_conf_t *>(child);

  ngx_conf_merge_value(conf->enable, prev->enable, 0);
  ngx_conf_merge_value(conf->enable_locations, prev->enable_locations, 1);

  if (prev->operation_name_script.is_valid() &&
      !conf->operation_name_script.is_valid())
    conf->operation_name_script = prev->operation_name_script;

  if (prev->loc_operation_name_script.is_valid() &&
      !conf->loc_operation_name_script.is_valid())
    conf->loc_operation_name_script = prev->loc_operation_name_script;

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
      datadog_tag_t* tag = &((datadog_tag_t*)prev->tags->elts)[i];
      std::string key;
      key.assign(reinterpret_cast<const char*>(tag->key_script.pattern_.data), tag->key_script.pattern_.len);
      merged_tags[key] = *tag;
    }

    for (ngx_uint_t i = 0; i < conf->tags->nelts; i++) {
      datadog_tag_t* tag = &((datadog_tag_t*)conf->tags->elts)[i];
      std::string key;
      key.assign(reinterpret_cast<const char*>(tag->key_script.pattern_.data), tag->key_script.pattern_.len);
      merged_tags[key] = *tag;
    }

    ngx_uint_t index = 0;
    for (const auto& kv : merged_tags) {
      if (index == conf->tags->nelts) {
        datadog_tag_t* tag = (datadog_tag_t*)ngx_array_push(conf->tags);

        if (!tag) {
          return (char*)NGX_CONF_ERROR;
        }

        *tag = kv.second;
      } 
      else {
        datadog_tag_t* tag = (datadog_tag_t*)conf->tags->elts;
        tag[index] = kv.second;
      }

      index++;
    }
  }

  return NGX_CONF_OK;
}
