You can set up tracing to include collecting trace information about proxies.

## NGINX open source

### Module installation

**Note**: this nginx module does not work on Linux distributions that use older versions of `glibc`. This includes RHEL/Centos 7 and AmazonLinux 1.
A workaround for this is to run NGINX from a Docker container. An example Dockerfile is available [here][2].

Datadog tracing is provided by a [dedicated nginx module][3].  To start tracing:

- download the appropriate version of the built module,
- copy the module into nginx's modules directory,
- load the module in nginx's configuration file.

For example, here is a script that installs the module:
```bash
# Get the latest release version of a specified github project.
get_latest_release() {
  url="https://api.github.com/repos/$1/releases/latest"
  wget --quiet -O - "$url" | jq --raw-output '.tag_name'
}

# Get the version number of the currently installed nginx.
get_nginx_version() {
    nginx -v 2>&1 | sed 's,.*\snginx/\([0-9.]\+\).*,\1,'
}

# Get the path to the directory where nginx looks for modules.
get_nginx_modules_path() {
    nginx -V 2>&1 | sed --quiet 's/.*--modules-path=\(\S*\).*/\1/p'
}

NGINX_VERSION=$(get_nginx_version)
NGINX_MODULES_PATH=$(get_nginx_modules_path)
DD_TRACE_NGINX_VERSION=$(get_latest_release TODO)

# Download the Datadog nginx tracing module and extract it into nginx's modules
# directory.
archive="linux-amd64-nginx-${NGINX_VERSION}-ngx_dd_trace_http_module.so.tgz"
wget "https://github.com/TODO/releases/download/$DD_TRACE_NGINX_VERSION/$archive"
tar zxf "$archive" -C "$NGINX_MODULES_PATH"
```

### NGINX configuration

The NGINX configuration must load the Datadog module.

```nginx
# Load the Datadog tracing module.
load_module modules/ngx_dd_trace_http_module.so;
```

The tracer is configured in the `http` block:

<!--```nginx
    # Configure distributed tracing with Datadog.
    datadog {
      "environment": "prod",
      "service": "nginx",
      "operation_name_override": "nginx.handle",
      "agent_host": "localhost",
      "agent_port": 8126
    }
```

TODO: Or maybe it looks like this:-->
```nginx
    # Configure distributed tracing with Datadog.
    datadog {
      environment prod;
      service nginx;
      operation_name_override nginx.handle;
      agent_host localhost;
      agent_port 8126;
    }
```

See a [complete example][5].

After completing this configuration, HTTP requests to NGINX will initiate and propagate Datadog traces, and will appear in the APM UI.

[2]: https://github.com/DataDog/dd-opentracing-cpp/blob/master/examples/nginx-tracing/Dockerfile
[3]: https://github.com/TODO
[5]: https://github.com/DataDog/TODO
