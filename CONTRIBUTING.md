# Developing `nginx-datadog`

This document describes the development process for `nginx-datadog`.
It is intended for anyone considering opening an issue or pull request.

Building Locally
----------------

```shell
NGINX_VERSION=1.29.7 make build
```

The resulting Nginx module is `.build/ngx_http_datadog_module.so`.

If you encounter some **difficulties** building the module on **MacOS**, please look at the [troubleshooting section](#Troubleshooting).

The `build` target does the following:

- Download a source release of nginx based on the `NGINX_VERSION` environment variable.
- Initialize the source tree of `dd-trace-cpp` as a git submodule.
- Initialize the source tree of `libddwaf`as a git submodule.
- Build `dd-trace-cpp` and the Datadog nginx module together using CMake.

`make clean` deletes CMake's build directory.

Testing
-------

The `Makefile` contains two target for testing:

- `build-and-test`: build and use the resultant module for testing.
- `test`: use the existing built module for testing.

To run one or the other, you can use:

### Linux, MacOS AMD64

```shell
NGINX_VERSION=1.29.7 make build-and-test
```

### MacOS with Apple Silicon

```shell
NGINX_VERSION=1.29.7 ARCH=aarch64 make build-and-test
```

By default, it will launch the test on the `nginx:${NGINX_VERSION}-alpine` Docker image.
If you want to use another Nginx image you can use:

```shell
BASE_IMAGE=nginx:1.29.7-alpine-slim make build-and-test
```

### Additional test options

To run the tests related to AppSec:

```shell
WAF=ON NGINX_VERSION=1.29.7 make build-and-test
```

To run the tests using an OpenResty image:

```shell
RESTY_VERSION=1.29.2.1 make test-openresty
```

You can also specificy the OpenResty base image rather then the version using the `BASE_IMAGE` parameter.

You can pass on arguments to test suites using:

```shell
TEST_ARGS="foo=bar" NGINX_VERSION=1.29.7 make test
```

For more information on tests, see [test/README.md](test/README.md).

Troubleshooting
----------------
### fatal error: 'pcre2.h' file not found on MacOS

If during the build of the module, you encounter this error, please ensure that pcre2 is installed on your device. If not, you can install it with:

```shell
brew install pcre2
```

If the build still does not work, you can use the flag `PCRE2_PATH` to specify the pcre2 installation folder it:

```shell
PCRE2_PATH=/opt/homebrew/Cellar/pcre2/10.44 NGINX_VERSION=1.29.7 make build
```
