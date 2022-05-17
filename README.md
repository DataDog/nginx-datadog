<img alt="datadog tracing nginx" src="mascot.svg" height="200"/>

Datadog Nginx Tracing Module
============================
This is the source for an nginx module that adds Datadog distributed tracing to
nginx.  The module is called `ngx_http_datadog_module`.

Usage
-----
Download `ngx_http_datadog_module.so` from a recent release, copy it to
wherever nginx looks for modules (e.g. `/usr/lib/nginx/modules/`) and add the
following line to the top of the main nginx configuration (e.g.
`/etc/nginx/nginx.conf`):
```nginx
load_module modules/ngx_http_datadog_module.so;
```
Tracing is automatically added to all endpoints by default.  For more
information, see [the API documentation](API.md).

Build
-----
TODO
