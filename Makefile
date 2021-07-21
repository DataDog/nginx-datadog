NGINX_VERSION = 1.19.10
MODULE_PATH := $(realpath module/)
MODULE_NAME := ngx_foo_module

CMakeLists.txt: nginx_build_info.json bin/generate_cmakelists.py
	bin/generate_cmakelists.py $(MODULE_NAME) >$@ <$<

nginx_build_info.json: nginx/objs/Makefile bin/makefile_database.py
	bin/makefile_database.py nginx/objs/Makefile >$@

# TODO: via Caleb: auto/configure takes signature-specific arguments.
nginx/objs/Makefile: nginx/ module/config
	cd nginx && auto/configure --add-dynamic-module=$(MODULE_PATH) --with-compat

nginx/:
	git clone --depth 1 --branch release-$(NGINX_VERSION) https://github.com/nginx/nginx

.PHONY: format
format:
	yapf -i makefile_database.py bin/generate_cmakelists.py

.PHONY: clean
clean:
	rm -rf nginx nginx_build_info.json .build
