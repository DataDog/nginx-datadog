NGINX_VERSION ?= 1.18
# TODO: Consider renaming the module/ directory.
MODULE_PATH := $(realpath module/)
MODULE_NAME = ngx_http_datadog_module
CLONE = git -c advice.detachedHead=false clone

.PHONY: build
build: prebuild
	mkdir -p .build && cd .build && cmake -DBUILD_TESTING=OFF .. && make -j VERBOSE=1
	@echo 'build successful 👍'

.PHONY: prebuild
prebuild: nginx-module.cmake dd-opentracing-cpp-deps

.PHONY: dd-opentracing-cpp-deps
dd-opentracing-cpp-deps: dd-opentracing-cpp/deps/include/curl dd-opentracing-cpp/deps/include/msgpack

dd-opentracing-cpp/deps/include/curl dd-opentracing-cpp/deps/include/msgpack: dd-opentracing-cpp/scripts/install_dependencies.sh
	cd dd-opentracing-cpp && ./scripts/install_dependencies.sh not-opentracing
	touch $@

nginx-module.cmake: nginx_build_info.json bin/generate_cmakelists.py
	bin/generate_cmakelists.py nginx_module >$@ <$<

nginx_build_info.json: nginx/objs/Makefile bin/makefile_database.py
	bin/makefile_database.py nginx/objs/Makefile $(MODULE_NAME) >$@

nginx/objs/Makefile: nginx/ $(MODULE_PATH)/config
	cd nginx && auto/configure --add-dynamic-module=$(MODULE_PATH) --with-compat
	
$(MODULE_PATH)/config: bin/module_config.sh
	bin/module_config.sh $(MODULE_NAME) >$@

nginx/:
	$(CLONE) --depth 1 --branch branches/stable-$(NGINX_VERSION) https://github.com/nginx/nginx

.PHONY: format
format:
	yapf -i bin/*.py

.PHONY: clean
clean:
	rm -rf \
		$(MODULE_PATH)/config \
		nginx_build_info.json \
		.build \
		nginx-module.cmake \
	
.PHONY: clobber
clobber: clean
	rm -rf \
	    nginx \
		dd-opentracing-cpp/deps

.PHONY: build-in-docker
build-in-docker: prebuild
	docker build --tag nginx-module-cmake-build .
	bin/run_in_build_image.sh bin/cmake_build.sh .docker-build

.PHONY: test
test: build-in-docker
# test: .build/libngx_http_datadog_module.so
	cd test && DD_API_KEY=$$(cat ~/.dd-keys/default/api-key) $(MAKE) up

.PHONY: test-config
test-config: build-in-docker
# test-config: .build/libngx_http_datadog_module.so
	cd test && $(MAKE) check-config

.build/libngx_http_datadog_module.so: prebuild
	bin/cmake_build.sh
