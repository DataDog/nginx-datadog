NGINX_VERSION = 1.18.0
NGINX_OPENTRACING_VERSION = 0.19.0
MODULE_PATH := $(realpath module/)
MODULE_NAME = ngx_http_opentracing_module
CLONE = git -c advice.detachedHead=false clone

.PHONY: all
all: nginx-opentracing/ nginx-module.cmake

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

nginx-opentracing/:
	$(CLONE) --depth 1 --branch v$(NGINX_OPENTRACING_VERSION) https://github.com/opentracing-contrib/nginx-opentracing

.PHONY: format
format:
	yapf -i bin/*.py

.PHONY: clean
clean:
	rm -rf nginx nginx-opentracing nginx_build_info.json .build nginx-module.cmake
