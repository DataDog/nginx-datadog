# Contributing to the Datadog Nginx Module

## Conventions

Follow [doc/conventions.md](doc/conventions.md).

## Format

- `make lint`: check format
- `make format` fix format

Rebuild formatter image after editing `Dockerfile.formatter` with `make build-formatter-image`.

## Update CI Images

The CI build, test, and uWSGI images are built for amd64 and arm64. CI uses image digests from
`.gitlab/common.yml` so that each pipeline uses immutable images.

To update the images:

1. Update the relevant image source:
   - `build_env/` for the Nginx build image.
   - `test/Dockerfile` for the test image.
   - `test/services/uwsgi/` for the uWSGI image.
2. When updating the shared musl toolchain, update `MUSL_TOOLCHAIN_IMAGE_DIGEST` and its version
   comment in `.gitlab/common.yml` before building the CI images.
3. Push the changes to a branch.
4. In the branch pipeline, run the manual `build-and-sign-ci-images` job. It builds both
   architectures, publishes multi-architecture images, signs them, and verifies the signatures.
5. Download the `ci-images.env` artifact from the `assemble-and-sign-ci-images` child-pipeline job,
   or copy the variables from its log.
6. Update `NGINX_CI_BUILD_IMAGE_DIGEST`, `TEST_IMAGE`, and `UWSGI_TEST_IMAGE` in
   `.gitlab/common.yml` with the generated digests.
7. Commit and push the digest updates. Verify that the new pipeline passes with the pinned images.

The formatter image uses a separate flow. CI hashes `Dockerfile.formatter` and builds the image
automatically when that hash is not already in the registry.

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
