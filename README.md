<img alt="datadog tracing nginx" src="mascot.svg" height="200"/>

Datadog Nginx Tracing Module
============================
This is the source for an nginx module that adds Datadog distributed tracing to
nginx. The module is called `ngx_http_datadog_module`.

Usage
-----
1. Download a gzipped tarball from a [recent release][12], extract it to wherever nginx
looks for modules (e.g. `/usr/lib/nginx/modules/`).
2. Add the following line to the top of the main nginx configuration (e.g.  `/etc/nginx/nginx.conf`):

```nginx
load_module modules/ngx_http_datadog_module.so;
```
Tracing is automatically added to all endpoints by default.  For more
information, see [the API documentation](doc/API.md).

Compatibility
-------------
> [!IMPORTANT]
> We provide support for NGINX versions up to their End Of Life, extended by one year. 
> [Aligned with the NGINX release cycle][11], this entails support for the four most recent NGINX versions.
>
> If you plan to add tracing features to an older NGINX version using our module, please check out [the build section](#build) for guidance.

There is one version of the module for each docker image we follow, which
include the following:

- Debian variants of [nginx's DockerHub images][3].
- Alpine variants of [nginx's DockerHub images][3].
- [Amazon Linux][10].

Each release contains one zipped tarball per supported image above.
The zipped tarball contains a single file, `ngx_http_datadog_module.so`, which is the Datadog tracing nginx module.

The naming convention is `<base image with underscores>-<arch>-ngx_http_datadog_module.so.tgz`.

Supported architectures (`<arch>`) are `amd64` and `arm64`.

Examples:
- `nginx_1.23.1-alpine-amd64-ngx_http_datadog_module.so.tgz` corresponds to NGINX version 1.23.1 docker image, running on Alpine, and designed for the amd64 architecture.
- `amazonlinux_2.0.20230119.1-arm64-ngx_http_datadog_module.so.tgz` refers to Amazon Linux version 2.0.20230119.1 docker image, designed for the arm64 architecture.


Default Behavior
----------------
Unless otherwise configured, `ngx_http_datadog_module` adds the following
default tracing behavior to nginx:
- Connect to the Datadog agent at `http://localhost:8126`.
- Create one span per request:
    - Service name is "nginx".
    - Operation name is "nginx.request".
    - Resource name is `"$request_method $uri"`, e.g. "GET /api/book/0-345-24223-8/title".
    - Includes multiple `http.*` [tags][8].

Custom configuration can be specified via the [datadog_*](doc/API.md)
family of directives in nginx's configuration file, or via [environment variables][9].

Build
-----
Requirements:
- C and C++ toolchain (`clang` or `gcc/g++`).
- CMake `v3.7` or newer.
- Architecture must be `x86_64` or `arm64`.

For enhanced usability, we provide a [GNU make][1] compatible [Makefile](Makefile).

```shell
NGINX_VERSION=1.25.2 make build
```

The resulting nginx module is `.build/ngx_http_datadog_module.so`

The `build` target does the following:

- Download a source release of nginx based on the `NGINX_VERSION` environment variable.
- Initialize the source tree of `dd-trace-cpp` as a git submodule.
- Build `dd-trace-cpp` and the Datadog nginx module together using
  CMake.

`make clean` deletes CMake's build directory. `make clobber` deletes
everything done by the build.

Build in Docker
---------------
```shell
make build-in-docker
```

`build-in-docker` target, builds the Datadog nginx module and its
dependencies in a [Docker][2] container compatible with the DockerHub image
specified as `BASE_IMAGE` in the `nginx-version-info` file, (e.g.
`nginx:1.19.1-alpine`) and with the nginx source version specified as
`NGINX_VERSION` in the `nginx-version-info` file (e.g. `1.19.1`).

The appropriate build image must be created first using the
[bin/docker_build.sh](bin/docker_build.sh) script if it does not exist already.
Once the image is built, `make build-in-docker` produces the nginx module as
`.docker-build/ngx_http_datadog_module.so`.

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
