.DELETE_ON_ERROR:

BUILD_DIR ?= .build
MAKE_JOB_COUNT ?= $(shell nproc)

.PHONY: build
build: build-deps sources
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake -DNGINX_SRC_DIR=nginx -DBUILD_TESTING=OFF .. && make -j $(MAKE_JOB_COUNT) VERBOSE=1
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
	    curl -s -S -L -o nginx.tar.gz "$(bin/nginx_release_downloads.sh $(NGINX_VERSION))" && \
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
build-in-docker: sources
	bin/run_in_build_image.sh make BUILD_DIR=.docker-build build

.PHONY: test
test: build-in-docker
	cp .docker-build/libngx_http_datadog_module.so test/services/nginx/ngx_http_datadog_module.so
	test/bin/run $(TEST_ARGS)

.PHONY: test-parallel
test-parallel: build-in-docker
	cp .docker-build/libngx_http_datadog_module.so test/services/nginx/ngx_http_datadog_module.so
	test/bin/run_parallel $(TEST_ARGS)

.PHONY: lab
lab: build-in-docker
	cp .docker-build/libngx_http_datadog_module.so lab/services/nginx/ngx_http_datadog_module.so
	lab/bin/run $(TEST_ARGS)
