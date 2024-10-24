.DELETE_ON_ERROR:

BUILD_DIR ?= .build
BUILD_TYPE ?= RelWithDebInfo
WAF ?= OFF
RUM ?= OFF
MAKE_JOB_COUNT ?= $(shell nproc)
PWD ?= $(shell pwd)
NGINX_SRC_DIR ?= $(PWD)/nginx
ARCH ?= $(shell arch)
COVERAGE ?= OFF
DOCKER_REPOS ?= public.ecr.aws/b1o7r7e0/nginx_musl_toolchain
CIRCLE_CFG ?= .circleci/continue_config.yml

SHELL := /bin/bash

.PHONY: build
build: build-deps sources
	# -DCMAKE_C_FLAGS=-I/opt/homebrew/Cellar/pcre2/10.42/include/ -DCMAKE_CXX_FLAGS=-I/opt/homebrew/Cellar/pcre2/10.42/include/ -DCMAKE_LDFLAGS=-L/opt/homebrew/Cellar/pcre2/10.42/lib -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
	cmake -B$(BUILD_DIR) -DNGINX_SRC_DIR=$(NGINX_SRC_DIR) \
		-DNGINX_COVERAGE=$(COVERAGE) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DNGINX_DATADOG_ASM_ENABLED=$(WAF) -DNGINX_DATADOG_RUM_ENABLED=$(RUM) . \
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
		.musl-build

.PHONY: clobber
clobber: clean
	rm -rf \
	    nginx

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
	docker build --progress=plain --platform $(DOCKER_PLATFORM) --build-arg ARCH=$(ARCH) -t $(DOCKER_REPOS) build_env

.PHONY: build-push-musl-toolchain
build-push-musl-toolchain:
	docker build --progress=plain --platform linux/amd64 --build-arg ARCH=x86_64 -t $(DOCKER_REPOS):latest-amd64 build_env
	docker push $(DOCKER_REPOS):latest-amd64
	docker build --progress=plain --platform linux/arm64 --build-arg ARCH=aarch64 -t $(DOCKER_REPOS):latest-arm64 build_env
	docker push $(DOCKER_REPOS):latest-arm64
	docker buildx imagetools create -t $(DOCKER_REPOS):latest \
		$(DOCKER_REPOS):latest-amd64 \
		$(DOCKER_REPOS):latest-arm64

.PHONY: build-musl
build-musl:
	docker run --init --rm \
		--platform $(DOCKER_PLATFORM) \
		--env ARCH=$(ARCH) \
		--env BUILD_TYPE=$(BUILD_TYPE) \
		--env NGINX_VERSION=$(NGINX_VERSION) \
		--env NGINX_SRC_DIR=$(NGINX_SRC_DIR) \
		--env WAF=$(WAF) \
		--env RUM=$(RUM) \
		--env COVERAGE=$(COVERAGE) \
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		$(DOCKER_REPOS):latest \
		make -C /mnt/repo build-musl-aux

# this is what's run inside the container nginx_musl_toolchain
.PHONY: build-musl-aux
build-musl-aux:
	cmake -B .musl-build \
		-DCMAKE_TOOLCHAIN_FILE=/sysroot/$(ARCH)-none-linux-musl/Toolchain.cmake \
		-DNGINX_PATCH_AWAY_LIBC=ON \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DNGINX_VERSION="$(NGINX_VERSION)" \
		-DNGINX_DATADOG_ASM_ENABLED="$(WAF)" . \
		-DNGINX_DATADOG_RUM_ENABLED="$(RUM)" . \
		-DNGINX_COVERAGE=$(COVERAGE) \
		&& cmake --build .musl-build -j $(MAKE_JOB_COUNT) -v

.PHONY: test
test: build-musl
	cp -v .musl-build/ngx_http_datadog_module.so* test/services/nginx/
	test/bin/run $(TEST_ARGS)

.PHONY: coverage
coverage:
	COVERAGE=ON $(MAKE) build-musl
	cp -v .musl-build/ngx_http_datadog_module.so* test/services/nginx/
	rm -f test/coverage_data.tar.gz
	test/bin/run --verbose --failfast
	docker run --init --rm --platform $(DOCKER_PLATFORM) \
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		$(DOCKER_REPOS):latest \
		tar -C /mnt/repo/.musl-build -xzf /mnt/repo/test/coverage_data.tar.gz
	docker run --init --rm --platform $(DOCKER_PLATFORM) \
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		$(DOCKER_REPOS):latest \
		bin/sh -c 'cd /mnt/repo/.musl-build; llvm-profdata merge -sparse *.profraw -o default.profdata && llvm-cov export ./ngx_http_datadog_module.so -format=lcov -instr-profile=default.profdata -ignore-filename-regex=/mnt/repo/src/coverage_fixup\.c > coverage.lcov'


.PHONY: test-parallel
test-parallel: build-in-docker
	cp -v .musl-build/ngx_http_datadog_module.so* test/services/nginx/
	test/bin/run_parallel $(TEST_ARGS)

.PHONY: lab
lab: build-musl
	cp -v .musl-build/ngx_http_datadog_module.so* lab/services/nginx/
	lab/bin/run $(TEST_ARGS)

.PHONY: circleci-config
circleci-config:
	@echo "Compiling circleci config"
	circleci config pack .circleci/src > $(CIRCLE_CFG)
	@echo "Validating circleci config"
	circleci config validate $(CIRCLE_CFG)

.PHONY: build-ingress-nginx
build-ingress-nginx:
	python3 bin/ingress_nginx.py prepare --ingress-nginx-version $(INGRESS_NGINX_VERSION) --output nginx-controller-$(INGRESS_NGINX_VERSION)
	docker run --init --rm \
		--platform $(DOCKER_PLATFORM) \
		--env ARCH=$(ARCH) \
		--env BUILD_TYPE=$(BUILD_TYPE) \
		--env NGINX_SRC_DIR=/mnt/repo/nginx-controller-$(INGRESS_NGINX_VERSION) \
		--env WAF=$(WAF) \
		--env COVERAGE=$(COVERAGE) \
		--mount "type=bind,source=$(PWD),destination=/mnt/repo" \
		$(DOCKER_REPOS):latest \
		make -C /mnt/repo build-musl-aux-ingress

.PHONY: build-musl-aux-ingress
build-musl-aux-ingress:
	cmake -B .musl-build \
		-DCMAKE_TOOLCHAIN_FILE=/sysroot/$(ARCH)-none-linux-musl/Toolchain.cmake \
		-DNGINX_PATCH_AWAY_LIBC=ON \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DNGINX_SRC_DIR="$(NGINX_SRC_DIR)" \
		-DNGINX_DATADOG_ASM_ENABLED="$(WAF)" \
		-DNGINX_COVERAGE=$(COVERAGE) . \
		&& cmake --build .musl-build -j $(MAKE_JOB_COUNT) -v
