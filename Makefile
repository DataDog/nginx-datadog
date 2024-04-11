.DELETE_ON_ERROR:

BUILD_DIR ?= .build
BUILD_TYPE ?= RelWithDebInfo
WAF ?= OFF
MAKE_JOB_COUNT ?= $(shell nproc)
PWD ?= $(shell pwd)

.PHONY: build
build: build-deps sources
	# -DCMAKE_C_FLAGS=-I/opt/homebrew/Cellar/pcre2/10.42/include/ -DCMAKE_CXX_FLAGS=-I/opt/homebrew/Cellar/pcre2/10.42/include/ -DCMAKE_LDFLAGS=-L/opt/homebrew/Cellar/pcre2/10.42/lib -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
	cmake -B$(BUILD_DIR) -DNGINX_SRC_DIR=$(PWD)/nginx -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DNGINX_DATADOG_ASM_ENABLED=$(WAF) . \
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
		.docker-build

.PHONY: clobber
clobber: clean
	rm -rf \
	    nginx

.PHONY: build-in-docker
build-in-docker:
	bin/run_in_build_image.sh make BUILD_DIR=.docker-build build

.PHONY: test
test: build-in-docker
	cp .docker-build/ngx_http_datadog_module.so test/services/nginx/ngx_http_datadog_module.so
	cp .docker-build/_deps/libddwaf-src/lib/libddwaf.so test/services/nginx/
	test/bin/run $(TEST_ARGS)

.PHONY: test-parallel
test-parallel: build-in-docker
	cp .docker-build/ngx_http_datadog_module.so test/services/nginx/ngx_http_datadog_module.so
	cp .docker-build/_deps/libddwaf-src/lib/libddwaf.so test/services/nginx/
	test/bin/run_parallel $(TEST_ARGS)

.PHONY: lab
lab: build-in-docker
	cp .docker-build/ngx_http_datadog_module.so lab/services/nginx/ngx_http_datadog_module.so
	cp .docker-build/_deps/libddwaf-src/lib/libddwaf.so test/services/nginx/
	lab/bin/run $(TEST_ARGS)
