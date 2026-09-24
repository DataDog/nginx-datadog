# Contributing to the Datadog Nginx Module

## Conventions

Follow [doc/conventions.md](doc/conventions.md).

## Format

- `make lint`: check clang-format and Python format
- `make format`: rewrite files to match

Rebuild formatter image after editing `Dockerfile.formatter` with `make build-formatter-image`.

## Static Analysis

C++ is analyzed with **clang-tidy-19** (pinned; alpine:3.23.4 / LLVM 19) using
the shared `.clang-tidy` baseline. Warnings are errors.

Do not run clang-tidy on the host. `compile_commands.json` must be produced by
the same container that runs tidy.

```shell
NGINX_VERSION=<version> make lint-tidy
```

This runs `bin/lint-tidy.sh`, which re-execs in Docker, configures CMake, builds
the module so generated nginx headers exist, then runs clang-tidy-19. Set
`WAF=ON` (the default in this script) to include AppSec sources.

The custom nginx log-format plugin is separate:

```shell
NGINX_VERSION=<version> make lint-nginx-log-format
```

GitLab jobs `lint-tidy` and `lint-nginx-log-format` run the same scripts and
fail the merge request pipeline on findings.

## Build Locally

```shell
NGINX_VERSION=<version> make build
```

The resulting Nginx module is `.build/ngx_http_datadog_module.so`.

If you encounter some difficulties building the module on **MacOS**, please look at the
[troubleshooting section](#troubleshooting).

The `build` target does the following:

- Download a source release of Nginx based on the `NGINX_VERSION` environment variable.
- Initialize the source tree of `dd-trace-cpp` as a git submodule.
- Initialize the source tree of `libddwaf` as a git submodule.
- Build `dd-trace-cpp` and the Datadog Nginx module together using CMake.

`make clean` deletes CMake's build directory.

## Build in Docker

```shell
NGINX_VERSION=<version> make build-musl
```

Append `TOOLCHAIN_DEPENDENCY=` to skip local toolchain image rebuild.

## Test

The `Makefile` has two targets for testing:

- `build-and-test`: build and use the resultant module for testing.
- `test`: use the existing built module for testing.

To run one or the other, you can use:

### Linux, MacOS AMD64

```shell
NGINX_VERSION=<version> make build-and-test
```

### MacOS with Apple Silicon

```shell
NGINX_VERSION=<version> ARCH=aarch64 make build-and-test
```

By default, it will launch the test on the `nginx:${NGINX_VERSION}-alpine` Docker image.
If you want to use another Nginx image you can use:

```shell
NGINX_VERSION=<version> BASE_IMAGE=nginx:<version>-alpine-slim make build-and-test
```

### Additional Test Options

Append `TEST_DEPENDENCY=` to skip uwsgi test image rebuild.

To run the tests related to AppSec, add `WAF=ON`:

```shell
WAF=ON NGINX_VERSION=<version> make build-and-test
```

To run the tests using an OpenResty image:

```shell
RESTY_VERSION=<version> BASE_IMAGE=nginx:<version> make test-openresty
```

You can also specify the OpenResty base image rather then the version using the `BASE_IMAGE`
parameter.

To build and run the tests with address sanitizer instrumentation:

```shell
ASAN=ON NGINX_VERSION=<version> make build-and-test
```

You can pass on arguments to test suites using:

```shell
TEST_ARGS="foo=bar" NGINX_VERSION=<version> make test
```

For example, to run one test:

```shell
TEST_ARGS="cases.package.module.TestClass.test_method" NGINX_VERSION=<version> make test
```

For more information on tests, see [test/README.md](test/README.md).

## Troubleshooting

### fatal error: 'pcre2.h' file not found on MacOS

If during the build of the module, you encounter this error, please ensure that pcre2 is installed
on your device. If not, you can install it with:

```shell
brew install pcre2
```

If the build still does not work, you can use the flag `PCRE2_PATH` to specify the pcre2
installation folder it:

```shell
PCRE2_PATH=/opt/homebrew/Cellar/pcre2/10.44 NGINX_VERSION=<version> make build
```
