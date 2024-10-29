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

# OpenResty ENV variables
RESTY_OPENSSL_PATCH_VERSION ?= "1.1.1f"
RESTY_OPENSSL_URL_BASE ?= "https://www.openssl.org/source/old/1.1.1"
RESTY_PCRE_VERSION ?= "8.45"
RESTY_PCRE_BUILD_OPTIONS ?= "--enable-jit"
RESTY_PCRE_SHA256 ?= "4e6ce03e0336e8b4a3d6c2b70b1c5e18590a5673a98186da90d4f33c23defc09"
RESTY_J ?= 8
RESTY_CONFIG_OPTIONS?="\
    --with-compat \
    --with-file-aio \
    --with-http_addition_module \
    --with-http_auth_request_module \
    --with-http_dav_module \
    --with-http_flv_module \
    --with-http_geoip_module=dynamic \
    --with-http_gunzip_module \
    --with-http_gzip_static_module \
    --with-http_image_filter_module=dynamic \
    --with-http_mp4_module \
    --with-http_random_index_module \
    --with-http_realip_module \
    --with-http_secure_link_module \
    --with-http_slice_module \
    --with-http_ssl_module \
    --with-http_stub_status_module \
    --with-http_sub_module \
    --with-http_v2_module \
    --with-http_xslt_module=dynamic \
    --with-ipv6 \
    --with-mail \
    --with-mail_ssl_module \
    --with-md5-asm \
    --with-sha1-asm \
    --with-stream \
    --with-stream_ssl_module \
    --with-threads \
"

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
		.openresty-build \
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

NGINX_VERSION ?= $(shell echo ${RESTY_VERSION} | cut -d '.' -f 1-3)

.PHONY: build-openresty-toolchain
build-openresty-toolchain:
ifndef RESTY_VERSION
	$(error RESTY_VERSION is not set. Please set RESTY_VERSION environment variable)
endif 
ifndef RESTY_OPENSSL_VERSION
	$(error RESTY_OPENSSL_VERSION is not set. Please set RESTY_OPENSSL_VERSION environment variable)
endif 
	docker build \
		--build-arg RESTY_VERSION=${RESTY_VERSION} \
		--build-arg RESTY_CONFIG_OPTIONS=${RESTY_CONFIG_OPTIONS} \
		--build-arg RESTY_CONFIG_OPTIONS_MORE=${RESTY_CONFIG_OPTIONS_MORE} \
		--build-arg RESTY_OPENSSL_URL_BASE=${RESTY_OPENSSL_URL_BASE} \
		--build-arg RESTY_OPENSSL_VERSION=${RESTY_OPENSSL_VERSION} \
		--build-arg RESTY_OPENSSL_PATCH_VERSION=${RESTY_OPENSSL_PATCH_VERSION} \
		--build-arg RESTY_PCRE_VERSION=${RESTY_PCRE_VERSION} \
		--build-arg RESTY_PCRE_BUILD_OPTIONS=${RESTY_PCRE_BUILD_OPTIONS} \
		--build-arg RESTY_PCRE_SHA256=${RESTY_PCRE_SHA256} \
		--build-arg RESTY_J=${RESTY_J} \
		-t louis/openresty-${RESTY_VERSION} \
		--no-cache \
		openresty/build

.PHONY: build-openresty
build-openresty:
ifndef RESTY_VERSION
	$(error RESTY_VERSION is not set. Please set RESTY_VERSION environment variable)
endif 
	docker run --rm \
		--platform $(DOCKER_PLATFORM) \
		--env WAF=$(WAF) \
 		--mount type=bind,source="$(PWD)",target=/tmp/nginx-datadog \
		--workdir="/tmp/nginx-datadog" \
		louis/openresty-${RESTY_VERSION} \
		bash -c "RESTY_VERSION=${RESTY_VERSION} make build-openresty-aux"

PHONY: build-openresty-aux
build-openresty-aux:
	cmake -B .openresty-build \
		-DNGINX_SRC_DIR=/tmp/openresty-${RESTY_VERSION}/build/nginx-${NGINX_VERSION} \
		-DCMAKE_C_FLAGS='-I /usr/local/openresty/pcre/include' \
		-DCMAKE_CXX_FLAGS='-I /usr/local/openresty/pcre/include' \
		-DNGINX_CONF_ARGS='--with-pcre=/usr/local/openresty/pcre --with-openssl=/usr/local/openresty/openssl' . \
		-DNGINX_DATADOG_ASM_ENABLED="$(WAF)" . \
	&& cmake --build .openresty-build -j $(MAKE_JOB_COUNT)

.PHONY: test
test: build-musl
	cp -v .musl-build/ngx_http_datadog_module.so* test/services/nginx/
	test/bin/run-nginx $(TEST_ARGS)

.PHONY: openresty-test
openresty-test: build-openresty
	cp -v .openresty-build/ngx_http_datadog_module.so* test/services/nginx
	test/bin/run-openresty $(TEST_ARGS)

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
	lab/bin/run-nginx-lab $(TEST_ARGS)

.PHONY: openresty-lab
openresty-lab: build-openresty
	cp -v .openresty-build/ngx_http_datadog_module.so* lab/services/nginx/
	lab/bin/run-openresty-lab $(TEST_ARGS)
	lab/bin/run $(TEST_ARGS)

.PHONY: circleci-config
circleci-config:
	@echo "Compiling circleci config"
	circleci config pack .circleci/src > $(CIRCLE_CFG)
	@echo "Validating circleci config"
	circleci config validate $(CIRCLE_CFG)
