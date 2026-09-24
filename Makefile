.DELETE_ON_ERROR:

BUILD_DIR ?= .build
BUILD_TESTING ?= ON
BUILD_TYPE ?= RelWithDebInfo
ASAN ?= OFF
MSAN ?= OFF
COVERAGE ?= OFF
MAKE_JOB_COUNT ?= $(shell nproc)
PWD ?= $(shell pwd)
RUM ?= OFF
WAF ?= OFF
ASAN_TEST_ARG = $(if $(filter ON TRUE true 1 YES yes,$(ASAN)),--asan --arch $(ARCH),)
MSAN_TEST_ARG = $(if $(filter ON TRUE true 1 YES yes,$(MSAN)),--msan --arch $(ARCH),)
TEST_IMAGE_ARG = $(if $(filter ON TRUE true 1 YES yes,$(ASAN))$(filter ON TRUE true 1 YES yes,$(MSAN)),,--image $${BASE_IMAGE:-nginx:$(NGINX_VERSION)-alpine})
MUSL_CLANG_CONFIG = $(if $(filter ON TRUE true 1 YES yes,$(ASAN)),test/services/nginx/musl-clang-asan.conf,$(if $(filter ON TRUE true 1 YES yes,$(MSAN)),test/services/nginx/musl-clang-msan.conf,))
MUSL_CLANG_CONFIG_ARG = $(if $(MUSL_CLANG_CONFIG),--volume "$(abspath $(MUSL_CLANG_CONFIG)):/etc/musl-clang.conf:ro",)
ifdef GITLAB_CI
MUSL_BUILD_DIR ?= .musl-build
else
MUSL_BUILD_SUFFIX = $(if $(filter ON TRUE true 1 YES yes,$(ASAN)),-asan,$(if $(filter ON TRUE true 1 YES yes,$(MSAN)),-msan,))
MUSL_BUILD_DIR ?= .musl-build$(MUSL_BUILD_SUFFIX)
endif

ARCH ?= $(shell arch)
# Normalize architecture names: CI uses amd64/arm64, build tools expect x86_64/aarch64
ifeq ($(ARCH),amd64)
	ARCH := x86_64
endif
ifeq ($(ARCH),arm64)
	ARCH := aarch64
endif

NGINX_SRC_DIR ?=
ifneq ($(strip $(NGINX_SRC_DIR)),)
	NGINX_SRC_DIR_DOCKER_ARGS := \
		--env NGINX_SRC_DIR=/mnt/nginx-src \
		--mount "type=bind,source=$(abspath $(NGINX_SRC_DIR)),destination=/mnt/nginx-src"
endif

DOCKER_PLATFORM := linux/$(ARCH)
ifeq ($(DOCKER_PLATFORM),linux/x86_64)
	DOCKER_PLATFORM := linux/amd64
endif
ifeq ($(DOCKER_PLATFORM),linux/aarch64)
	DOCKER_PLATFORM := linux/arm64
endif

ifneq ($(PCRE2_PATH),)
	CMAKE_PCRE_OPTIONS := -DCMAKE_C_FLAGS=-I$(PCRE2_PATH)/include/ -DCMAKE_CXX_FLAGS=-I$(PCRE2_PATH)/include/ -DCMAKE_LDFLAGS=-L$(PCRE2_PATH)/lib
endif

# Detect if we're already running inside Docker or CI
IN_DOCKER_OR_CI := $(shell if [ "$(IN_DOCKER)" = "true" ] || \
			[ -f /.dockerenv ] || \
			[ "$$KUBERNETES_SERVICE_PORT_HTTPS" ] || \
			[ "$$GITLAB_CI" ]; then echo "true"; \
			else echo "false"; fi)


# ----- Docker Images

MUSL_TOOLCHAIN_IMAGE_DIGEST := $(shell sed -n 's/^  MUSL_TOOLCHAIN_IMAGE_DIGEST: "\([^"]*\)".*$$/\1/p' .gitlab/common.yml)
CI_REGISTRY := registry.ddbuild.io/ci/nginx-datadog
UWSGI_TEST_IMAGE := $(CI_REGISTRY)/uwsgi

FORMATTER_IMAGE ?= nginx-datadog-formatter

ifdef GITLAB_CI
	MUSL_TOOLCHAIN_IMAGE ?= registry.ddbuild.io/ci/musl-toolchain-glibc-support/musl-build-env@$(MUSL_TOOLCHAIN_IMAGE_DIGEST)
	BUILD_IMAGE ?= $(NGINX_CI_BUILD_IMAGE)
	TOOLCHAIN_DEPENDENCY :=
	TEST_DEPENDENCY :=
else
	MUSL_TOOLCHAIN_IMAGE ?= public.ecr.aws/datadog/musl-build-env@$(MUSL_TOOLCHAIN_IMAGE_DIGEST)
	BUILD_IMAGE ?= nginx_musl_toolchain
	TOOLCHAIN_DEPENDENCY := build-local-musl-toolchain
	TEST_DEPENDENCY := build-local-uwsgi-test-image
endif
export MUSL_TOOLCHAIN_IMAGE

.PHONY: build-local-musl-toolchain
build-local-musl-toolchain:
	docker build --progress=plain --platform $(DOCKER_PLATFORM) \
		--build-arg MUSL_TOOLCHAIN_IMAGE=$(MUSL_TOOLCHAIN_IMAGE) \
		--tag $(BUILD_IMAGE) build_env

.PHONY: build-local-uwsgi-test-image
build-local-uwsgi-test-image:
	docker build --progress=plain --platform $(DOCKER_PLATFORM) -t $(UWSGI_TEST_IMAGE):latest test/services/uwsgi

# ----- Sources Dependencies, Format and Lint

.PHONY: dd-trace-cpp-deps
dd-trace-cpp-deps: dd-trace-cpp/.git

dd-trace-cpp/.git:
	git submodule update --init --recursive

dd-trace-cpp/.clang-format: dd-trace-cpp/.git

.clang-format: dd-trace-cpp/.clang-format

# Docker run command for the formatter (empty when already in Docker/CI)
FORMATTER_WORKDIR := -v $(CURDIR):$(CURDIR) -w $(CURDIR)
ifeq ($(shell uname -s),Linux)
	FORMATTER_USER_OPTS := --user "$(shell id -u):$(shell id -g)"
else
	FORMATTER_USER_OPTS :=
endif
ifeq ($(IN_DOCKER_OR_CI),true)
	FORMATTER_RUN :=
else
	FORMATTER_RUN := docker run --rm --platform $(DOCKER_PLATFORM) $(FORMATTER_USER_OPTS) $(FORMATTER_WORKDIR) $(FORMATTER_IMAGE)
endif

.PHONY: format
format: ensure-formatter-image .clang-format
	$(FORMATTER_RUN) bin/format.sh

.PHONY: lint
lint: ensure-formatter-image .clang-format
	$(FORMATTER_RUN) bin/lint.sh

.PHONY: lint-nginx-log-format
lint-nginx-log-format:
	bin/nginx-log-format-tidy.sh

.PHONY: ensure-formatter-image
ensure-formatter-image:
ifeq ($(IN_DOCKER_OR_CI),false)
	@if ! docker image inspect $(FORMATTER_IMAGE) > /dev/null 2>&1; then \
		echo "Formatter image not found, building..."; \
		$(MAKE) build-formatter-image; \
	fi
else
	@echo "Skipping: already in Docker/CI"
endif

.PHONY: build-formatter-image
build-formatter-image:
	docker build --platform $(DOCKER_PLATFORM) $(if $(filter environment command,$(origin MIRROR_REGISTRY)),--build-arg MIRROR_REGISTRY=$(MIRROR_REGISTRY),) -t $(FORMATTER_IMAGE) -f Dockerfile.formatter .


# ----- Build

.PHONY: clean
clean:
	rm -rf \
		.build \
		.musl-build \
		.musl-build-asan \
		.musl-build-msan \
		.openresty-build

.PHONY: build
build: dd-trace-cpp-deps
	cmake -B $(BUILD_DIR) -DNGINX_VERSION=$(NGINX_VERSION) \
		-DNGINX_SRC_DIR="$(NGINX_SRC_DIR)" \
		-DNGINX_COVERAGE=$(COVERAGE) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DNGINX_DATADOG_ASM_ENABLED=$(WAF) -DNGINX_DATADOG_RUM_ENABLED=$(RUM) . \
		-DBUILD_TESTING=$(BUILD_TESTING) $(CMAKE_PCRE_OPTIONS)\
		&& cmake --build $(BUILD_DIR) -j $(MAKE_JOB_COUNT) -v
	chmod 755 $(BUILD_DIR)/ngx_http_datadog_module.so
	@echo 'build successful 👍'

.PHONY: build-musl build-musl-cov
build-musl build-musl-cov: $(TOOLCHAIN_DEPENDENCY)
ifndef NGINX_VERSION
	$(error NGINX_VERSION is not set. Please set the NGINX_VERSION environment variable)
endif
ifdef GITLAB_CI
	$(MAKE) $@-aux
else
	docker run --init --rm \
		--platform $(DOCKER_PLATFORM) \
		--env ARCH=$(ARCH) \
		--env BUILD_TYPE=$(BUILD_TYPE) \
		--env NGINX_VERSION=$(NGINX_VERSION) \
		$(NGINX_SRC_DIR_DOCKER_ARGS) \
		--env WAF=$(WAF) \
		--env RUM=$(RUM) \
		--env ASAN=$(ASAN) \
		--env MSAN=$(MSAN) \
		--env COVERAGE=$(COVERAGE) \
		$(MUSL_CLANG_CONFIG_ARG) \
		--mount "type=bind,source=$(dir $(lastword $(MAKEFILE_LIST))),destination=/mnt/repo" \
		$(BUILD_IMAGE) \
		make -C /mnt/repo $@-aux
endif

.PHONY: build-musl-aux build-musl-cov-aux
build-musl-aux build-musl-cov-aux:
	CC=musl-clang CXX=musl-clang++ cmake -B $(MUSL_BUILD_DIR) \
		-DNGINX_VERIFY_NEEDED=ON \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DNGINX_VERSION="$(NGINX_VERSION)" \
		-DNGINX_SRC_DIR="$(NGINX_SRC_DIR)" \
		-DNGINX_DATADOG_ASM_ENABLED="$(WAF)" . \
		-DNGINX_DATADOG_RUM_ENABLED="$(RUM)" . \
		-DNGINX_COVERAGE=$(COVERAGE) \
		-DENABLE_ASAN="$(ASAN)" \
		-DENABLE_MSAN="$(MSAN)" . \
		&& cmake --build $(MUSL_BUILD_DIR) -j $(MAKE_JOB_COUNT) -v --target ngx_http_datadog_module \
		$(if $(filter build-musl-cov-aux,$@),&& cmake --build $(MUSL_BUILD_DIR) -j $(MAKE_JOB_COUNT) -v --target unit_tests)

# --- OpenResty

NGINX_VERSION ?= $(if $(RESTY_VERSION),$(shell echo $(RESTY_VERSION) | awk -F. '{print $$1"."$$2"."$$3}'))
BUILD_OPENRESTY_COMMAND := ./bin/openresty/build_openresty.sh && make build-openresty-aux
.PHONY: build-openresty
build-openresty: $(TOOLCHAIN_DEPENDENCY)
ifndef RESTY_VERSION
	$(error RESTY_VERSION is not set. Please set the RESTY_VERSION environment variable)
endif
ifdef GITLAB_CI
	bash -c "$(BUILD_OPENRESTY_COMMAND)"
else
	docker run --init --rm \
		--platform $(DOCKER_PLATFORM) \
		--env ARCH=$(ARCH) \
		--env BUILD_TYPE=$(BUILD_TYPE) \
		--env RESTY_VERSION=$(RESTY_VERSION) \
		--env NGINX_VERSION=$(NGINX_VERSION) \
		--env WAF=$(WAF) \
		$(MUSL_CLANG_CONFIG_ARG) \
		--mount type=bind,source="$(PWD)",destination=/mnt/repo \
		$(BUILD_IMAGE) \
		bash -c "cd /mnt/repo && $(BUILD_OPENRESTY_COMMAND)"
endif

.PHONY: build-openresty-aux
build-openresty-aux:
	CC=musl-clang CXX=musl-clang++ cmake -B .openresty-build \
		-DNGINX_VERIFY_NEEDED=ON \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DNGINX_SRC_DIR=/tmp/openresty-${RESTY_VERSION}/build/nginx-${NGINX_VERSION} \
		-DNGINX_DATADOG_FLAVOR="openresty" \
		-DNGINX_DATADOG_ASM_ENABLED="$(WAF)" . \
		&& cmake --build .openresty-build -j $(MAKE_JOB_COUNT) -v --target ngx_http_datadog_module \

# --- Ingress Nginx

.PHONY: build-ingress-nginx
build-ingress-nginx: $(TOOLCHAIN_DEPENDENCY)
ifndef INGRESS_NGINX_VERSION
	$(error INGRESS_NGINX_VERSION is not set. Please set the INGRESS_NGINX_VERSION environment variable)
endif
	python3 bin/ingress_nginx.py prepare --ingress-nginx-version v$(INGRESS_NGINX_VERSION) --output nginx-controller-$(INGRESS_NGINX_VERSION)
ifdef GITLAB_CI
	$(MAKE) build-musl-aux-ingress
else
	docker run --init --rm \
		--platform $(DOCKER_PLATFORM) \
		--env ARCH=$(ARCH) \
		--env BUILD_TYPE=$(BUILD_TYPE) \
		--env INGRESS_NGINX_VERSION=$(INGRESS_NGINX_VERSION) \
		--env WAF=$(WAF) \
		--env COVERAGE=$(COVERAGE) \
		$(MUSL_CLANG_CONFIG_ARG) \
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		$(BUILD_IMAGE) \
		make -C /mnt/repo build-musl-aux-ingress
endif

.PHONY: build-musl-aux-ingress
build-musl-aux-ingress:
	CC=musl-clang CXX=musl-clang++ cmake -B .musl-build \
		-DNGINX_VERIFY_NEEDED=ON \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DNGINX_SRC_DIR=nginx-controller-$(INGRESS_NGINX_VERSION) \
		-DNGINX_DATADOG_ASM_ENABLED="$(WAF)" \
		-DNGINX_COVERAGE=$(COVERAGE) . \
		-DNGINX_DATADOG_FLAVOR="ingress-nginx" \
		&& cmake --build .musl-build -j $(MAKE_JOB_COUNT) -v


# ----- Test

.PHONY: build-and-test
build-and-test: build-musl test

.PHONY: test
test: $(TEST_DEPENDENCY)
	uv run --project test test/bin/run.py $(TEST_IMAGE_ARG) \
		--module-path $(MUSL_BUILD_DIR)/ngx_http_datadog_module.so $(ASAN_TEST_ARG) $(MSAN_TEST_ARG) -- \
		--verbose $(TEST_ARGS)

.PHONY: build-and-test-openresty
build-and-test-openresty: build-openresty test-openresty

.PHONY: test-openresty
test-openresty: $(TEST_DEPENDENCY)
	RESTY_TEST=ON uv run --project test test/bin/run.py --image ${BASE_IMAGE} \
		--module-path .openresty-build/ngx_http_datadog_module.so -- \
		--verbose $(TEST_ARGS)

.PHONY: example-openresty
example-openresty: build-openresty
	cp -v .openresty-build/ngx_http_datadog_module.so* example/openresty/services/openresty
	./example/openresty/bin/run

.PHONY: coverage
coverage:
ifndef GITLAB_CI
	$(error make coverage should be run only from GitLab CI)
endif
ifneq ($(ARCH),x86_64)
	$(error make coverage supports only amd64)
endif
	COVERAGE=ON BUILD_TESTING=ON $(MAKE) build-musl-cov
	cd $(MUSL_BUILD_DIR); LLVM_PROFILE_FILE=unit_tests.profraw test/unit/unit_tests
	rm -f test/coverage_data.tar.gz
	uv run --project test test/bin/run.py --image ${BASE_IMAGE} --module-path $(MUSL_BUILD_DIR)/ngx_http_datadog_module.so -- --verbose --failfast
	tar -C $(MUSL_BUILD_DIR) -xzf test/coverage_data.tar.gz
	cd $(MUSL_BUILD_DIR); llvm-profdata merge -sparse *.profraw -o default.profdata && llvm-cov export ./ngx_http_datadog_module.so -format=lcov -instr-profile=default.profdata -ignore-filename-regex=src/coverage_fixup\.c > coverage.lcov
	# dd-sts latest version: https://github.com/DataDog/dd-source/blob/main/domains/seceng/sit/apps/apis/dd-sts/cmd/cli/version.bzl#L6
	apk add --no-cache gcompat
	wget --quiet https://binaries.ddbuild.io/dd-source/dd-sts/v0.1.5/dd-sts-tar.tar.gz
	tar -xzf dd-sts-tar.tar.gz
	install -m 0755 dd-sts-linux-amd64 /usr/local/bin/dd-sts
	# datadog-ci versions: https://github.com/DataDog/datadog-ci/releases
	# datadog-ci packages: https://www.npmjs.com/package/@datadog/datadog-ci?activeTab=versions
	dd-sts exchange --policy apm-sdks-api-key -- npx --yes @datadog/datadog-ci@5.18.0 coverage upload --format=lcov $(MUSL_BUILD_DIR)/coverage.lcov
