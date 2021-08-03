nginxtracing
============
User Configuration
------------------
```nginx
load_module modules/ngx_tracing_datadog_module.so;

events {
    worker_connections  1024;
}

http {
    tracing_configure {
          "service": "nginx",
          "operation_name_override": "nginx.handle",
          "agent_host": "localhost",
          "agent_port": 8126
    }

    tracing_tag http_user_agent $http_user_agent;
    tracing_operation_name "$request_method $uri";

    log_format with_trace_id '$remote_addr - $http_x_forwarded_user [$time_local] "$request" '
        '$status $body_bytes_sent "$http_referer" '
        '"$http_user_agent" "$http_x_forwarded_for" '
        '"$tracing_context_x_datadog_trace_id" "$tracing_context_x_datadog_parent_id"';

    access_log /var/log/nginx/access.log with_trace_id;

    server {
        listen       80;
        server_name  localhost;

        location / {
            root   /usr/share/nginx/html;
            index  index.html index.htm;
            tracing_tag "custom-tag" "special value";
        }

        location /test {
            alias /usr/share/nginx/html/index.html;
        }
    }
}
```
Implementor Configuration
-------------------------
```c++
namespace nginxtracing {

// The `TracingLibrary` interface is a set of policies that allow a tracing
// implementation to configure nginx's tracing behavior. 
class TracingLibrary {
    // Return a `Tracer` created with the specified `configuration`.  If an
    // error occurs, return `nullptr` and assign a diagnostic to the specified `error`.
    virtual std::shared_ptr<opentracing::Tracer> MakeTracer(opentracing::string_view configuration, std::string &error) const = 0;

    // Return the names of span tags relevant to trace context propagation.
    virtual std::vector<std::string> SpanTagNames() const = 0;

    // Return the names of environment variables for worker processes to inherit
    // from the main nginx executable.
    virtual std::vector<std::string> EnvironmentVariableNames() const = 0;
    
    // Return the default setting for whether tracing is enabled in nginx.
    bool TracingOnByDefault() const = 0;

    // Return the default setting for whether HTTP locations generate a trace.
    bool TraceLocationsByDefault() const = 0;

    // Return whether to allow the tracer JSON configuration inline within
    // the nginx configuration using the `opentracing_configure` directive.
    // If `false`, then the `opentracing_configuration_file` directive must be
    // used instead.
    bool ConfigureTracerJSONInline() const = 0;
};

// A tracing library implementor must provide a definition for the `library`
// function.
extern const TracingLibrary& library();

} // namespace nginxtracing
```

Build Setup
-----------
```text
repo dd-nginx-module/
├── bin
├── .build
│   └── libngx_tracing_datadog_module.so
├── CMakeLists.txt
├── Makefile (produces nginx-module.cmake)
├── nginx-module.cmake
├── nginx-opentracing.cmake
├── submodule dd-opentracing-cpp
├── submodule nginx
├── submodule nginxtracing (fork nginx-opentracing)
└── submodule opentracing-cpp
```
