NGINX_VERSION = 1.18.0
# TODO: Consider renaming the module/ directory.
MODULE_PATH := $(realpath module/)
MODULE_NAME = ngx_http_opentracing_module
CLONE = git -c advice.detachedHead=false clone

.PHONY: all
all: nginx-module.cmake dd-opentracing-cpp-deps

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
	
$(MODULE_PATH)/config:
	bin/module_config.sh $(MODULE_NAME) >$@

nginx/:
	$(CLONE) --depth 1 --branch release-$(NGINX_VERSION) https://github.com/nginx/nginx

.PHONY: format
format:
	yapf -i bin/*.py

.PHONY: clean
clean:
	rm -rf nginx nginx_build_info.json .build nginx-module.cmake
