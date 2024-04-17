.DELETE_ON_ERROR:

BUILD_DIR ?= .build
BUILD_TYPE ?= RelWithDebInfo
WAF ?= OFF
MAKE_JOB_COUNT ?= $(shell nproc)
PWD ?= $(shell pwd)
NGINX_SRC_DIR ?= $(PWD)/nginx
ARCH ?= $(shell arch)

SHELL := /bin/bash

.PHONY: build
build: build-deps sources
	# -DCMAKE_C_FLAGS=-I/opt/homebrew/Cellar/pcre2/10.42/include/ -DCMAKE_CXX_FLAGS=-I/opt/homebrew/Cellar/pcre2/10.42/include/ -DCMAKE_LDFLAGS=-L/opt/homebrew/Cellar/pcre2/10.42/lib -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
	cmake -B$(BUILD_DIR) -DNGINX_SRC_DIR=$(NGINX_SRC_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DNGINX_DATADOG_ASM_ENABLED=$(WAF) . \
		&& cmake --build $(BUILD_DIR) -j $(MAKE_JOB_COUNT) -v
	chmod 755 $(BUILD_DIR)/ngx_http_datadog_module.so
	@echo 'build successful 👍'

.PHONY: sources
sources: dd-trace-cpp/.git nginx/

.PHONY: build-deps
build-deps: sources dd-trace-cpp-deps

dd-trace-cpp/.git:
	git submodule update --init --recursive

.PHONY: dd-trace-cpp-deps
dd-trace-cpp-deps: dd-trace-cpp/.git

nginx/:
ifndef NGINX_VERSION
	$(error NGINX_VERSION is not set. Please set NGINX_VERSION environment variable)
endif
	rm -rf nginx && \
	    curl -s -S -L -o nginx.tar.gz "$(shell bin/nginx_release_downloads.sh $(NGINX_VERSION))" && \
		mkdir nginx && \
		tar xzf nginx.tar.gz -C nginx --strip-components 1 && \
		rm nginx.tar.gz

dd-trace-cpp/.clang-format: dd-trace-cpp/.git

.clang-format: dd-trace-cpp/.clang-format

.PHONY: format
format: .clang-format
	bin/format.sh

.PHONY: lint
lint: .clang-format
	bin/lint.sh

.PHONY: clean
clean:
	rm -rf \
		.build \
		.docker-build \
		.musl-build

.PHONY: clobber
clobber: clean
	rm -rf \
	    nginx

.PHONY: build-in-docker
# docker build cannot use nginx already checked out because it likely was
# configured already on the host
build-in-docker:
ifndef NGINX_VERSION
	$(error NGINX_VERSION is not set. Please set NGINX_VERSION environment variable)
endif
	bin/run_in_build_image.sh make NGINX_SRC_DIR= BUILD_DIR=.docker-build build

DOCKER_PLATFORM := linux/$(ARCH)
ifeq ($(DOCKER_PLATFORM),linux/x86_64)
	DOCKER_PLATFORM := linux/amd64
endif
ifeq ($(DOCKER_PLATFORM),linux/aarch64)
	DOCKER_PLATFORM := linux/arm64
endif

# For testing changes to the build image
.PHONY: build-musl-toolchain
build-musl-toolchain:
	docker build --progress=plain --platform $(DOCKER_PLATFORM) --build-arg ARCH=$(ARCH) -t gustavolopes557/nginx_musl_toolchain build_env

.PHONY: build-push-musl-toolchain
build-push-musl-toolchain:
	docker build --progress=plain --platform linux/amd64 --build-arg ARCH=x86_64 -t gustavolopes557/nginx_musl_toolchain-amd64 build_env
	docker push gustavolopes557/nginx_musl_toolchain-amd64
	docker build --progress=plain --platform linux/arm64 --build-arg ARCH=aarch64 -t gustavolopes557/nginx_musl_toolchain-arm64 build_env
	docker push gustavolopes557/nginx_musl_toolchain-arm64
	#docker manifest create gustavolopes557/nginx_musl_toolchain \
		gustavolopes557/nginx_musl_toolchain-amd64 \
		gustavolopes557/nginx_musl_toolchain-arm64
	#docker manifest push gustavolopes557/nginx_musl_toolchain

.PHONY: build-musl
build-musl:
	docker run --rm \
		--platform $(DOCKER_PLATFORM) \
		--env NGINX_VERSION=$(NGINX_VERSION) \
		--env WAF=$(WAF) \
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		gustavolopes557/nginx_musl_toolchain \
		make -C /mnt/repo build-musl-aux

# this is what's run inside the container nginx_musl_toolchain
.PHONY: build-musl-aux
build-musl-aux:
	cmake -B .musl-build \
		-DCMAKE_TOOLCHAIN_FILE=/sysroot/$(ARCH)-none-linux-musl/Toolchain.cmake \
		-DNGINX_PATCH_AWAY_LIBC=ON \
		-DNGINX_VERSION="$(NGINX_VERSION)" \
		-DNGINX_DATADOG_ASM_ENABLED="$(WAF)" . \
		&& cmake --build .musl-build -j $(MAKE_JOB_COUNT) -v


.PHONY: test
test: build-in-docker
	cp .docker-build/ngx_http_datadog_module.so test/services/nginx/ngx_http_datadog_module.so
	test/bin/run $(TEST_ARGS)

.PHONY: test-parallel
test-parallel: build-in-docker
	cp .docker-build/ngx_http_datadog_module.so test/services/nginx/ngx_http_datadog_module.so
	test/bin/run_parallel $(TEST_ARGS)

.PHONY: lab
lab: build-in-docker
	cp .docker-build/ngx_http_datadog_module.so lab/services/nginx/ngx_http_datadog_module.so
	lab/bin/run $(TEST_ARGS)
