.DELETE_ON_ERROR:

NGINX_VERSION = $(shell cat nginx-version)
# TODO: Consider renaming the module/ directory.
MODULE_PATH := $(realpath module/)
MODULE_NAME = ngx_http_datadog_module
BUILD_DIR ?= .build

.PHONY: build
build: build-deps
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake -DBUILD_TESTING=OFF .. && make -j VERBOSE=1
	@echo 'build successful 👍'

.PHONY: sources
sources: dd-opentracing-cpp/.git opentracing-cpp/.git nginx/

.PHONY: build-deps
build-deps: sources nginx-module.cmake dd-opentracing-cpp-deps

dd-opentracing-cpp/.git opentracing-cpp/.git:
	git submodule update --init --recursive

dd-opentracing-cpp/scripts/install_dependencies.sh: dd-opentracing-cpp/.git

.PHONY: dd-opentracing-cpp-deps
dd-opentracing-cpp-deps: dd-opentracing-cpp/deps/include/curl dd-opentracing-cpp/deps/include/msgpack dd-opentracing-cpp/.git

dd-opentracing-cpp/deps/include/curl dd-opentracing-cpp/deps/include/msgpack: dd-opentracing-cpp/scripts/install_dependencies.sh
	cd dd-opentracing-cpp && ./scripts/install_dependencies.sh not-opentracing
	touch $@

nginx-module.cmake: nginx_build_info.json bin/generate_cmakelists.py
	bin/generate_cmakelists.py nginx_module >$@ <$<

nginx_build_info.json: nginx/objs/Makefile bin/makefile_database.py
	bin/makefile_database.py nginx/objs/Makefile $(MODULE_NAME) >$@

nginx/objs/Makefile: nginx/ $(MODULE_PATH)/config
	cd nginx && ./configure --add-dynamic-module=$(MODULE_PATH) --with-compat
	
$(MODULE_PATH)/config: bin/module_config.sh
	bin/module_config.sh $(MODULE_NAME) >$@

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
	yapf3 -i bin/*.py
	test/bin/format

.PHONY: clean
clean:
	rm -rf \
		$(MODULE_PATH)/config \
		nginx_build_info.json \
		.build \
		.docker-build \
		nginx-module.cmake \
	
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
