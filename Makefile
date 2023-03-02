.DELETE_ON_ERROR:

# Including this file defines NGINX_VERSION.
include nginx-version-info

BUILD_DIR ?= .build
MAKE_JOB_COUNT ?= $(shell nproc)

.PHONY: build
build: build-deps nginx/objs/Makefile sources
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake -DBUILD_TESTING=OFF .. && make -j $(MAKE_JOB_COUNT) VERBOSE=1
	chmod 755 $(BUILD_DIR)/libngx_http_datadog_module.so
	@echo 'build successful 👍'

.PHONY: sources
sources: dd-opentracing-cpp/.git opentracing-cpp/.git nginx/

.PHONY: build-deps
build-deps: sources dd-opentracing-cpp-deps

dd-opentracing-cpp/.git opentracing-cpp/.git:
	git submodule update --init --recursive

dd-opentracing-cpp/scripts/install_dependencies.sh: dd-opentracing-cpp/.git

.PHONY: dd-opentracing-cpp-deps
dd-opentracing-cpp-deps: dd-opentracing-cpp/deps/include/curl dd-opentracing-cpp/deps/include/msgpack dd-opentracing-cpp/.git

dd-opentracing-cpp/deps/include/curl dd-opentracing-cpp/deps/include/msgpack: dd-opentracing-cpp/scripts/install_dependencies.sh
	ls -lrt
	ls -lrt dd-opentracing-cpp
	ls -lrt dd-opentracing-cpp/scripts
	cd dd-opentracing-cpp && ls ./scripts && ./scripts/install_dependencies.sh not-opentracing
	touch $@

nginx/objs/Makefile: nginx/ module/config
	cd nginx && ./configure --add-dynamic-module=../module/ --with-compat

nginx/: nginx-version-info
	rm -rf nginx && \
	    curl -s -S -L -o nginx.tar.gz "$(shell bin/nginx_release_downloads.sh $(NGINX_VERSION))" && \
		mkdir nginx && \
		tar xzf nginx.tar.gz -C nginx --strip-components 1 && \
		rm nginx.tar.gz

# In the "build" target, we use ./nginx-version-info just for the nginx version
# (to download the appropriate sources).
# In the "build-in-docker" target, we use ./nginx-version-info to also identify
# the appropriate base image.
nginx-version-info:
	$(error The file "nginx-version-info" must be present and contain definitions for NGINX_VERSION and BASE_IMAGE")

dd-opentracing-cpp/.clang-format: dd-opentracing-cpp/.git

.clang-format: dd-opentracing-cpp/.clang-format
	cp $< $@

.PHONY: format
format: .clang-format
	find src/ -type f \( -name '*.h' -o -name '*.cpp' \) -not \( -name 'json.h' -o -name 'json.cpp' \) -print0 | xargs -0 clang-format-14 -i --style=file
	find bin/ -type f -name '*.py' -print0 | xargs -0 yapf3 -i
	test/bin/format

.PHONY: clean
clean:
	rm -rf \
		.build \
		.docker-build

.PHONY: clobber
clobber: clean
	rm -rf \
		nginx \
		dd-opentracing-cpp/deps \
		dd-opentracing-cpp/CMakeFiles

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
