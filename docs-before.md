You can set up tracing to include collecting trace information about proxies.

## NGINX open source

### Plugin installation

**Note**: this plugin does not work on Linux distributions that use older versions of `libstdc++`. This includes RHEL/Centos 7 and AmazonLinux 1.
A workaround for this is to run NGINX from a Docker container. An example Dockerfile is available [here][2].

The following plugins must be installed:

- NGINX plugin for OpenTracing - [linux-amd64-nginx-${NGINX_VERSION}-ot16-ngx_http_module.so.tgz][3] - installed in `/usr/lib/nginx/modules`
- Datadog OpenTracing C++ Plugin - [linux-amd64-libdd_opentracing_plugin.so.gz][4] - installed somewhere accessible to NGINX, eg `/usr/local/lib`

Commands to download and install these modules:

```bash
# Gets the latest release version number from Github.
get_latest_release() {
  wget -qO- "https://api.github.com/repos/$1/releases/latest" |
    grep '"tag_name":' |
    sed -E 's/.*"([^"]+)".*/\1/';
}
NGINX_VERSION=1.17.3
OPENTRACING_NGINX_VERSION="$(get_latest_release opentracing-contrib/nginx-opentracing)"
DD_OPENTRACING_CPP_VERSION="$(get_latest_release DataDog/dd-opentracing-cpp)"
# Install NGINX plugin for OpenTracing
wget https://github.com/opentracing-contrib/nginx-opentracing/releases/download/${OPENTRACING_NGINX_VERSION}/linux-amd64-nginx-${NGINX_VERSION}-ot16-ngx_http_module.so.tgz
tar zxf linux-amd64-nginx-${NGINX_VERSION}-ot16-ngx_http_module.so.tgz -C /usr/lib/nginx/modules
# Install Datadog Opentracing C++ Plugin
wget https://github.com/DataDog/dd-opentracing-cpp/releases/download/${DD_OPENTRACING_CPP_VERSION}/linux-amd64-libdd_opentracing_plugin.so.gz
gunzip linux-amd64-libdd_opentracing_plugin.so.gz -c > /usr/local/lib/libdd_opentracing_plugin.so
```

### NGINX configuration

The NGINX configuration must load the OpenTracing module.

```nginx
# Load OpenTracing module
load_module modules/ngx_http_opentracing_module.so;
```

The `http` block enables the OpenTracing module and loads the Datadog tracer:

```nginx
    opentracing on; # Enable OpenTracing
    opentracing_tag http_user_agent $http_user_agent; # Add a tag to each trace!
    opentracing_trace_locations off; # Emit only one span per request.

    # Load the Datadog tracing implementation, and the given config file.
    opentracing_load_tracer /usr/local/lib/libdd_opentracing_plugin.so /etc/nginx/dd-config.json;
```

The `log_format with_trace_id` block is for correlating logs and traces. See the [example NGINX config][5] file for the complete format. The `$opentracing_context_x_datadog_trace_id` value captures the trace ID, and `$opentracing_context_x_datadog_parent_id` captures the span ID. 

The `location` block within the server where tracing is desired should add the following:

```nginx
            opentracing_operation_name "$request_method $uri";
            opentracing_propagate_context;
```

A config file for the Datadog tracing implementation is also required:

```json
{
  "environment": "prod",
  "service": "nginx",
  "operation_name_override": "nginx.handle",
  "agent_host": "localhost",
  "agent_port": 8126
}
```

The `service` value can be modified to a meaningful value for your usage of NGINX.
The `agent_host` value may need to be changed if NGINX is running in a container or orchestrated environment.

Complete examples:

* [nginx.conf][5]
* [dd-config.json][6]

After completing this configuration, HTTP requests to NGINX will initiate and propagate Datadog traces, and will appear in the APM UI.

#### NGINX and FastCGI

When the location is serving a FastCGI backend instead of HTTP, the `location` block should use `opentracing_fastcgi_propagate_context` instead of `opentracing_propagate_context`.

[1]: http://nginx.org/en/linux_packages.html#stable
[2]: https://github.com/DataDog/dd-opentracing-cpp/blob/master/examples/nginx-tracing/Dockerfile
[3]: https://github.com/opentracing-contrib/nginx-opentracing/releases/latest
[4]: https://github.com/DataDog/dd-opentracing-cpp/releases/latest
[5]: https://github.com/DataDog/dd-opentracing-cpp/blob/master/examples/nginx-tracing/nginx.conf
[6]: https://github.com/DataDog/dd-opentracing-cpp/blob/master/examples/nginx-tracing/dd-config.json
[7]: https://github.com/kubernetes/ingress-nginx

