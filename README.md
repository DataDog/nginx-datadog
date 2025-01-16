<img alt="datadog tracing nginx" src="mascot.svg" height="200"/>

[![codecov](https://codecov.io/gh/DataDog/nginx-datadog/graph/badge.svg?token=SZCZI1FAYU)](https://codecov.io/gh/DataDog/nginx-datadog)
# Datadog NGINX Module
This repository contains the source code for the `ngx_http_datadog_module`, an NGINX module
that integrates Datadog [APM][1] and [Application Security Management][2] into NGINX.

## Usage
1. Download a gzipped tarball from a [recent release][12], extract it to
   wherever nginx looks for modules (e.g. `/usr/lib/nginx/modules/`).
2. Add the following line to the top of the main nginx configuration (e.g.
   `/etc/nginx/nginx.conf`):

```nginx
load_module modules/ngx_http_datadog_module.so;
```

Tracing is automatically added to all endpoints by default. For more
information, see [the API documentation](doc/API.md).

## Compatibility
> [!IMPORTANT]
> We provide support for NGINX versions up to their End Of Life, extended by one
> year.  [Aligned with the NGINX release cycle][4], this entails support for
> the four most recent NGINX versions.
>
> If you plan to add tracing features to an older NGINX version using our
> module, please check out [the build section](#build) for guidance.

There are two tarballs (the actual executable module and, separately, the debug
symbols) per each combination of: 1) nginx version, 2) architecture, 3) whether
AppSec is built in or not. The main tarball contains a single file,
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

While it _may_ be possible to build the extension against an older version, this
is not guaranteed; in particular, AppSec builds require a feature introduced in
version 1.21.4.

## Default Behavior
Unless otherwise configured, `ngx_http_datadog_module` adds the following
default behavior to NGINX:

### Tracing
- Connect to the Datadog agent at `http://localhost:8126`.
- Create one span per request:
    - Service name is "nginx".
    - Operation name is "nginx.request".
    - Resource name is `"$request_method $uri"`, e.g. "GET /api/book/0-345-24223-8/title".
    - Includes multiple `http.*` [tags][5].

Custom configuration can be specified via the [datadog\_*](doc/API.md) family of
directives in nginx's configuration file, or via [environment variables][6].

## Enabling AppSec

To enable AppSec, besides using the correct binary (the relase artifact with
"-appsec") in the name, it's necessary to edit the nginx configuration:

* Set `datadog_appsec_enabled on;`.
* Define one (or more thread pools).
* Choose which thread pool AppSec will use, either on a global or a per-location
  basis.

For more information, see [the documentation](doc/API.md).

## Building the module
If the version of NGINX you’re using is no longer supported by this repository,
you can build the module by following the steps below.

This repository uses [git submodules][7] for some of its dependencies.
To ensure all dependencies are available or updated before building, run the
following command:

```shell
git submodule update --init --recursive
```

### Prerequisites
Before building the module, ensure your environment meets the following requirements:

- Recent C and C++ toolchain (`clang` or `gcc/g++`) (must support at least some
  C++20 features).
- make.
- CMake `v3.24` or newer.
- Architecture is either `x86_64` or `arm64`.

### Building using Docker
We recommend using Docker which greatly simplify the build process for various environments.
Below are specific commands and options for different build targets.

> [!IMPORTANT]
> Be sure to match the version of NGINX, OpenResty, or ingress-nginx with the version you 
> are using in your environment to avoid compatibility issues.

#### Building for NGINX
> [!NOTE]
> The `build-musl` target builds against [musl](https://www.musl-libc.org/) to guarantee portability.

```shell
WAF=ON ARCH=amd64 NGINX_VERSION=1.27.1 make build-musl
```

Options:
  - `WAF=<ON|OFF>`: Enable (`ON`) or disable (`OFF`) AppSec.
  - `ARCH=<amd64|aarch64>`: Specify the CPU architecture.
  - `NGINX_VERSION=<version>`: Specify the NGINX version to build.

The NGINX module will be generated at `.musl-build\ngx_http_datadog_module.so`.

### Building for OpenResty using Docker
> [!NOTE]
> The `build-openresty` target builds against [musl](https://www.musl-libc.org/) to guarantee portability.

To build the module for OpenResty:

```shell
WAF=ON ARCH=amd64 RESTY_VERSION=1.27.1.1 make build-openresty
```

Options:
  - `WAF=<ON|OFF>`: Enable (`ON`) or disable (`OFF`) AppSec.
  - `ARCH=<amd64|aarch64>`: Specify the CPU architecture.
  - `RESTY_VERSION=<version>`: Specify the OpenResty version to build.

### Building for ingress-nginx using Docker
> [!NOTE]
> The `build-ingress-nginx` target builds against [musl](https://www.musl-libc.org/) to guarantee portability.

To build the module for [ingress-nginx][8]:

```shell
WAF=ON ARCH=amd64 INGRESS_NGINX_VERSION=1.11.2 make build-ingress-nginx
```

Options:
  - `WAF=<ON|OFF>`: Enable (`ON`) or disable (`OFF`) AppSec.
  - `ARCH=<x86_64|aarch64>`: Specify the CPU architecture.
  - `INGRESS_NGINX_VERSION=<version>`: Specify the version [ingress-nginx][8] to build.

## Acknowledgements
This project is based largely on previous work. See [CREDITS.md](CREDITS.md).

[1]: https://docs.datadoghq.com/tracing/
[2]: https://docs.datadoghq.com/security/application_security/
[3]: https://github.com/DataDog/nginx-datadog/releases
[4]: https://www.nginx.com/blog/nginx-1-18-1-19-released/
[5]: https://github.com/DataDog/nginx-datadog/blob/535a291ce96d8ca80cb12b22febac1e138e45847/src/tracing_library.cpp#L187-L203
[6]: https://github.com/DataDog/dd-trace-cpp/blob/main/include/datadog/environment.h
[7]: https://git-scm.com/book/en/v2/Git-Tools-Submodules
[8]: https://github.com/kubernetes/ingress-nginx
<!-- vim: set tw=80: -->
