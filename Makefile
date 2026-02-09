.DELETE_ON_ERROR:

BUILD_DIR ?= .build
BUILD_TESTING ?= ON
BUILD_TYPE ?= RelWithDebInfo
CIRCLE_CFG ?= .circleci/config.yml
COVERAGE ?= OFF
MAKE_JOB_COUNT ?= $(shell nproc)
PWD ?= $(shell pwd)
RUM ?= OFF
WAF ?= OFF

ARCH ?= $(shell arch)
# Normalize architecture names (macOS uses arm64, Linux tools use aarch64)
ifeq ($(ARCH),amd64)
	ARCH := x86_64
endif
ifeq ($(ARCH),arm64)
	ARCH := aarch64
endif

NGINX_SRC_DIR ?= $(PWD)/nginx

ifneq ($(PCRE2_PATH),)
	CMAKE_PCRE_OPTIONS := -DCMAKE_C_FLAGS=-I$(PCRE2_PATH)/include/ -DCMAKE_CXX_FLAGS=-I$(PCRE2_PATH)/include/ -DCMAKE_LDFLAGS=-L$(PCRE2_PATH)/lib
endif


# ----- Docker Images

DOCKER_PLATFORM := linux/$(ARCH)
ifeq ($(DOCKER_PLATFORM),linux/x86_64)
	DOCKER_PLATFORM := linux/amd64
endif
ifeq ($(DOCKER_PLATFORM),linux/aarch64)
	DOCKER_PLATFORM := linux/arm64
endif

# On CircleCI, we build locally the nginx_musl_toolchain Docker image, before using it in some
#   targets via $(TOOLCHAIN_DEPENDENCY).
# On GitLab, we get the nginx_musl_toolchain Docker image from registry.ddbuild.io.
BUILD_IMAGE := nginx_musl_toolchain
CI_REGISTRY := registry.ddbuild.io/ci/nginx-datadog
CI_BUILD_IMAGE := $(CI_REGISTRY)/$(BUILD_IMAGE)
CI_TEST_IMAGE := $(CI_REGISTRY)/test
ifdef GITLAB_CI
	TOOLCHAIN_DEPENDENCY :=
else
	TOOLCHAIN_DEPENDENCY := build-local-musl-toolchain
endif

# build-push-musl-toolchain and build-push-test-image must be run, once, from a
#   developer machine, to put in registry.ddbuild.io the needed Docker images.
.PHONY: build-push-musl-toolchain
build-push-musl-toolchain:
	docker build --progress=plain --platform linux/amd64 --build-arg ARCH=x86_64 -t $(CI_BUILD_IMAGE):latest-amd64 build_env
	docker push $(CI_BUILD_IMAGE):latest-amd64
	docker build --progress=plain --platform linux/arm64 --build-arg ARCH=aarch64 -t $(CI_BUILD_IMAGE):latest-arm64 build_env
	docker push $(CI_BUILD_IMAGE):latest-arm64
	docker buildx imagetools create -t $(CI_BUILD_IMAGE):latest $(CI_BUILD_IMAGE):latest-amd64 $(CI_BUILD_IMAGE):latest-arm64

.PHONY: build-local-musl-toolchain
build-local-musl-toolchain:
	docker build --progress=plain --platform $(DOCKER_PLATFORM) --build-arg ARCH=$(ARCH) -t $(BUILD_IMAGE) build_env

.PHONY: build-push-test-image
build-push-test-image:
	docker build --progress=plain --platform linux/amd64 --build-arg ARCH=x86_64 -t $(CI_TEST_IMAGE):latest-amd64 test
	docker push $(CI_TEST_IMAGE):latest-amd64
	docker build --progress=plain --platform linux/arm64 --build-arg ARCH=aarch64 -t $(CI_TEST_IMAGE):latest-arm64 test
	docker push $(CI_TEST_IMAGE):latest-arm64
	docker buildx imagetools create -t $(CI_TEST_IMAGE):latest $(CI_TEST_IMAGE):latest-amd64 $(CI_TEST_IMAGE):latest-arm64


# ----- Sources Dependencies, Format and Lint

.PHONY: dd-trace-cpp-deps
dd-trace-cpp-deps: dd-trace-cpp/.git

dd-trace-cpp/.git:
	git submodule update --init --recursive

dd-trace-cpp/.clang-format: dd-trace-cpp/.git

.clang-format: dd-trace-cpp/.clang-format

.PHONY: format
format: .clang-format
	bin/format.sh

.PHONY: lint
lint: .clang-format
	bin/lint.sh

.PHONY: circleci-config
circleci-config:
	@echo "Compiling circleci config"
	circleci config pack .circleci/src > $(CIRCLE_CFG)
	@echo "Validating circleci config"
	circleci config validate $(CIRCLE_CFG)


# ----- Build

.PHONY: clean
clean:
	rm -rf \
		.build \
		.musl-build \
		.openresty-build

.PHONY: build
build: dd-trace-cpp-deps
	cmake -B $(BUILD_DIR) -DNGINX_VERSION=$(NGINX_VERSION) \
		-DNGINX_COVERAGE=$(COVERAGE) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DNGINX_DATADOG_ASM_ENABLED=$(WAF) -DNGINX_DATADOG_RUM_ENABLED=$(RUM) . \
		-DBUILD_TESTING=$(BUILD_TESTING) $(CMAKE_PCRE_OPTIONS)\
		&& cmake --build $(BUILD_DIR) -j $(MAKE_JOB_COUNT) -v
	chmod 755 $(BUILD_DIR)/ngx_http_datadog_module.so
	@echo 'build successful 👍'

.PHONY: build-musl build-musl-cov
build-musl build-musl-cov: $(TOOLCHAIN_DEPENDENCY)
ifndef NGINX_VERSION
	$(error NGINX_VERSION is not set. Please set the NGINX_VERSION environment variable.)
endif
ifdef GITLAB_CI
	$(MAKE) $@-aux
else
	docker run --init --rm \
		--platform $(DOCKER_PLATFORM) \
		--env ARCH=$(ARCH) \
		--env BUILD_TYPE=$(BUILD_TYPE) \
		--env NGINX_VERSION=$(NGINX_VERSION) \
		--env NGINX_SRC_DIR=$(NGINX_SRC_DIR) \
		--env WAF=$(WAF) \
		--env RUM=$(RUM) \
		--env COVERAGE=$(COVERAGE) \
		--mount "type=bind,source=$(dir $(lastword $(MAKEFILE_LIST))),destination=/mnt/repo" \
		$(BUILD_IMAGE) \
		make -C /mnt/repo $@-aux
endif

.PHONY: build-musl-aux build-musl-cov-aux
build-musl-aux build-musl-cov-aux:
	cmake -B .musl-build \
		-DCMAKE_TOOLCHAIN_FILE=/sysroot/$(ARCH)-none-linux-musl/Toolchain.cmake \
		-DNGINX_PATCH_AWAY_LIBC=ON \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DNGINX_VERSION="$(NGINX_VERSION)" \
		-DNGINX_DATADOG_ASM_ENABLED="$(WAF)" . \
		-DNGINX_DATADOG_RUM_ENABLED="$(RUM)" . \
		-DNGINX_COVERAGE=$(COVERAGE) \
		&& cmake --build .musl-build -j $(MAKE_JOB_COUNT) -v --target ngx_http_datadog_module \
		$(if $(filter build-musl-cov-aux,$@),&& cmake --build .musl-build -j $(MAKE_JOB_COUNT) -v --target unit_tests)

# --- OpenResty

NGINX_VERSION ?= $(if $(RESTY_VERSION),$(shell echo $(RESTY_VERSION) | awk -F. '{print $$1"."$$2"."$$3}'))
BUILD_OPENRESTY_COMMAND := ./bin/openresty/build_openresty.sh && make build-openresty-aux
.PHONY: build-openresty
build-openresty: $(TOOLCHAIN_DEPENDENCY)
ifndef RESTY_VERSION
	$(error RESTY_VERSION is not set. Please set the RESTY_VERSION environment variable.)
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
		--mount type=bind,source="$(PWD)",destination=/mnt/repo \
		$(BUILD_IMAGE) \
		bash -c "cd /mnt/repo && $(BUILD_OPENRESTY_COMMAND)"
endif

.PHONY: build-openresty-aux
build-openresty-aux:
	cmake -B .openresty-build \
		-DCMAKE_TOOLCHAIN_FILE=/sysroot/$(ARCH)-none-linux-musl/Toolchain.cmake \
		-DNGINX_PATCH_AWAY_LIBC=ON \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DNGINX_SRC_DIR=/tmp/openresty-${RESTY_VERSION}/build/nginx-${NGINX_VERSION} \
		-DNGINX_DATADOG_FLAVOR="openresty" \
		-DNGINX_DATADOG_ASM_ENABLED="$(WAF)" . \
		&& cmake --build .openresty-build -j $(MAKE_JOB_COUNT) -v --target ngx_http_datadog_module \

# --- Ingress Nginx

.PHONY: build-ingress-nginx
build-ingress-nginx: $(TOOLCHAIN_DEPENDENCY)
ifndef INGRESS_NGINX_VERSION
	$(error INGRESS_NGINX_VERSION is not set. Please set the INGRESS_NGINX_VERSION environment variable.)
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
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		$(BUILD_IMAGE) \
		make -C /mnt/repo build-musl-aux-ingress
endif

.PHONY: build-musl-aux-ingress
build-musl-aux-ingress:
	cmake -B .musl-build \
		-DCMAKE_TOOLCHAIN_FILE=/sysroot/$(ARCH)-none-linux-musl/Toolchain.cmake \
		-DNGINX_PATCH_AWAY_LIBC=ON \
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
test:
	python3 test/bin/run.py --image ${BASE_IMAGE} \
		--module-path .musl-build/ngx_http_datadog_module.so -- \
		--verbose $(TEST_ARGS)

.PHONY: build-and-test-openresty
build-and-test-openresty: build-openresty test-openresty

.PHONY: test-openresty
test-openresty:
	RESTY_TEST=ON python3 test/bin/run.py --image ${BASE_IMAGE} \
		--module-path .openresty-build/ngx_http_datadog_module.so -- \
		--verbose $(TEST_ARGS)

.PHONY: example-openresty
example-openresty: build-openresty
	cp -v .openresty-build/ngx_http_datadog_module.so* example/openresty/services/openresty
	./example/openresty/bin/run

.PHONY: coverage
coverage: $(TOOLCHAIN_DEPENDENCY)
	COVERAGE=ON BUILD_TESTING=ON $(MAKE) build-musl-cov
	docker run --init --rm --platform $(DOCKER_PLATFORM) \
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		$(BUILD_IMAGE) \
		/bin/sh -c 'cd /mnt/repo/.musl-build; LLVM_PROFILE_FILE=unit_tests.profraw test/unit/unit_tests'
	rm -f test/coverage_data.tar.gz
	python3 test/bin/run.py --image ${BASE_IMAGE} --module-path .musl-build/ngx_http_datadog_module.so -- --verbose --failfast
	docker run --init --rm --platform $(DOCKER_PLATFORM) \
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		$(BUILD_IMAGE) \
		tar -C /mnt/repo/.musl-build -xzf /mnt/repo/test/coverage_data.tar.gz
	docker run --init --rm --platform $(DOCKER_PLATFORM) \
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		$(BUILD_IMAGE) \
		/bin/sh -c 'cd /mnt/repo/.musl-build; llvm-profdata merge -sparse *.profraw -o default.profdata && llvm-cov export ./ngx_http_datadog_module.so -format=lcov -instr-profile=default.profdata -ignore-filename-regex=/mnt/repo/src/coverage_fixup\.c > coverage.lcov'
