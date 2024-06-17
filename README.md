<img alt="datadog tracing nginx" src="mascot.svg" height="200"/>

Datadog NGINX Module
====================
This repository contains the source code for the `ngx_http_datadog_module`, an NGINX module
that integrates Datadog [APM][13] and [Application Security Management][14] into NGINX.

Usage
-----
1. Download a gzipped tarball from a [recent release][12], extract it to
   wherever nginx looks for modules (e.g. `/usr/lib/nginx/modules/`).
2. Add the following line to the top of the main nginx configuration (e.g.
   `/etc/nginx/nginx.conf`):

```nginx
load_module modules/ngx_http_datadog_module.so;
```

Tracing is automatically added to all endpoints by default. For more
information, see [the API documentation](doc/API.md).

Compatibility
-------------
> [!IMPORTANT]
> We provide support for NGINX versions up to their End Of Life, extended by one
> year.  [Aligned with the NGINX release cycle][11], this entails support for
> the four most recent NGINX versions.
>
> If you plan to add tracing features to an older NGINX version using our
> module, please check out [the build section](#build) for guidance.

There are two tarballs (the actual executable module and, separately, the debug
symbols) per each combination of: 1) nginx version, 2) architecture, 3) whether
AppSec is built in or not.  The main tarball contains a single file,
`ngx_http_datadog_module.so`, which is the Datadog nginx module.

The naming convention is:

* `ngx_http_datadog_module-<arch>-<version>.so.tgz` for builds without appsec
  support and
* `ngx_http_datadog_module-appsec-<arch>-<version>.so.tgz` for builds with
  appsec support.

> [!IMPORTANT]
> The AppSec variants require nginx to have been built with `--threads` (thread
> support).

Supported architectures (`<arch>`) are `amd64` and `arm64`.


Default Behavior
----------------
Unless otherwise configured, `ngx_http_datadog_module` adds the following
default behavior to nginx:

### Tracing
- Connect to the Datadog agent at `http://localhost:8126`.
- Create one span per request:
    - Service name is "nginx".
    - Operation name is "nginx.request".
    - Resource name is `"$request_method $uri"`, e.g. "GET /api/book/0-345-24223-8/title".
    - Includes multiple `http.*` [tags][8].


Custom configuration can be specified via the [datadog\_*](doc/API.md) family of
directives in nginx's configuration file, or via [environment variables][9].

Enabling AppSec
---------------

To enable AppSec, besides using the correct binary (the relase artifact with
"-appsec") in the name, it's necessary to edit the nginx configuration:

* Set `datadog_appsec_enabled yes;`.
* Define one (or more thread pools).
* Choose which thread pool AppSec will use, either on a global or a per-location
  basis.

For more information, see [the documentation](doc/API.md).

Build
-----
Requirements:
- Recent C and C++ toolchain (`clang` or `gcc/g++`) (must support at least some
  C++20 features).
- CMake `v3.24` or newer.
- Architecture must be `x86_64` or `arm64`.

For enhanced usability, we provide a [GNU make][1] compatible [Makefile](Makefile).

```shell
NGINX_VERSION=1.25.2 make build
```

You can set the environment variable `WAF` to `ON` to build an AppSec-supporting
module:

```shell
WAF=ON NGINX_VERSION=1.25.2 make build
```

The resulting nginx module is `.build/ngx\_http\_datadog\_module.so`

The `build` target does the following:

- Download a source release of nginx based on the `NGINX\_VERSION` environment variable.
- Initialize the source tree of `dd-trace-cpp` as a git submodule.
- Build `dd-trace-cpp` and the Datadog nginx module together using
  CMake.

`make clean` deletes CMake's build directory. `make clobber` deletes
everything done by the build.

Build in Docker
---------------
```shell
make build-musl
```

The `build-musl` target builds against musl and libc++ a glibc-compatible
module. The Dockerfile for the docker image used in the process can be found in
[build_env/Dockerfile](./build_env/Dockerfile).

Test
----
See [test/README.md](test/README.md).

Acknowledgements
----------------
This project is based largely on previous work.  See [CREDITS.md](CREDITS.md).

[1]: https://www.gnu.org/software/make/
[2]: https://www.docker.com/
[3]: https://hub.docker.com/_/nginx?tab=tags
[4]: https://cmake.org/
[5]: https://hub.docker.com/layers/nginx/library/nginx/1.19.1-alpine/images/sha256-966f134cf5ddeb12a56ede0f40fff754c0c0a749182295125f01a83957391d84
[6]: https://www.gnu.org/software/libc/
[7]: https://www.musl-libc.org/
[8]: https://github.com/DataDog/nginx-datadog/blob/535a291ce96d8ca80cb12b22febac1e138e45847/src/tracing_library.cpp#L187-L203
[9]: https://github.com/DataDog/dd-trace-cpp/blob/main/src/datadog/environment.h
[10]: https://hub.docker.com/_/amazonlinux
[11]: https://www.nginx.com/blog/nginx-1-18-1-19-released/
[12]: https://github.com/DataDog/nginx-datadog/releases
[13]: https://docs.datadoghq.com/tracing/
[14]: https://docs.datadoghq.com/security/application_security/

<!-- vim: set tw=80: -->
