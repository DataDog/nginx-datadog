# Agent Instructions

`make format` to format, `make lint` to check. Rebuild formatter image after editing `Dockerfile.formatter` with `make build-formatter-image`.

## Running tests

See README.md#running-tests for concise, up-to-date commands. Briefly:
- Build once: NGINX_VERSION=<ver> make build-musl (append TOOLCHAIN_DEPENDENCY= to skip local toolchain image rebuild)
- Full suite: NGINX_VERSION=<ver> TOOLCHAIN_DEPENDENCY= TEST_DEPENDENCY= make build-and-test (WAF=ON to include AppSec)
- One test: TEST_ARGS="cases.package.module.TestClass.test_method" TEST_DEPENDENCY= make test
- ASAN mode: ASAN=ON ARCH=<x86_64|aarch64> NGINX_VERSION=<ver> TOOLCHAIN_DEPENDENCY= TEST_DEPENDENCY= make build-and-test (BASE_IMAGE is ignored)
