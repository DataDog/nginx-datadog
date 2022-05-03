.DELETE_ON_ERROR:

NGINX_VERSION = $(shell cat nginx-version)
BUILD_DIR ?= .build

.PHONY: build
build: build-deps
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake -DBUILD_TESTING=OFF .. && make -j VERBOSE=1
	@echo 'build successful 👍'

.PHONY: sources
sources: dd-opentracing-cpp/.git opentracing-cpp/.git nginx/ nginx/objs/Makefile

.PHONY: build-deps
build-deps: sources dd-opentracing-cpp-deps

dd-opentracing-cpp/.git opentracing-cpp/.git:
	git submodule update --init --recursive

dd-opentracing-cpp/scripts/install_dependencies.sh: dd-opentracing-cpp/.git

.PHONY: dd-opentracing-cpp-deps
dd-opentracing-cpp-deps: dd-opentracing-cpp/deps/include/curl dd-opentracing-cpp/deps/include/msgpack dd-opentracing-cpp/.git

dd-opentracing-cpp/deps/include/curl dd-opentracing-cpp/deps/include/msgpack: dd-opentracing-cpp/scripts/install_dependencies.sh
	cd dd-opentracing-cpp && ./scripts/install_dependencies.sh not-opentracing
	touch $@

nginx/objs/Makefile: nginx/ module/config
	cd nginx && ./configure --add-dynamic-module=../module/ --with-compat
	
nginx/: nginx-version
	rm -rf nginx && \
	    curl -s -S -L -o nginx.tar.gz "$(shell bin/nginx_release_downloads.sh $(NGINX_VERSION))" && \
		mkdir nginx && \
		tar xzf nginx.tar.gz -C nginx --strip-components 1 && \
		rm nginx.tar.gz

dd-opentracing-cpp/.clang-format: dd-opentracing-cpp/.git
	
.clang-format: dd-opentracing-cpp/.clang-format
	cp $< $@

.PHONY: format
format: .clang-format
	find src/ -type f \( -name '*.h' -o -name '*.cpp' \) -not \( -name 'json.h' -o -name 'json.cpp' \) -print0 | xargs -0 clang-format-9 -i --style=file	
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
		dd-opentracing-cpp/deps

.PHONY: build-in-docker
build-in-docker: sources
	docker build --tag nginx-module-cmake-build .
	bin/run_in_build_image.sh $(MAKE) BUILD_DIR=.docker-build build

.PHONY: test
test: build-in-docker
	cp .docker-build/libngx_http_datadog_module.so test/services/nginx/ngx_http_datadog_module.so
	test/bin/run $(TEST_ARGS)

.PHONY: test-parallel
test-parallel: build-in-docker
	cp .docker-build/libngx_http_datadog_module.so test/services/nginx/ngx_http_datadog_module.so
	test/bin/run_parallel $(TEST_ARGS)
