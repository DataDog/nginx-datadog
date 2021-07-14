NGINX_VERSION = 1.19.10
MODULE_PATH := $(realpath module/)

nginx_build_info.json: nginx/objs/Makefile makefile_database.py
	./makefile_database.py nginx/objs/Makefile >$@

nginx/objs/Makefile: nginx/ module/config
	cd nginx && auto/configure --add-dynamic-module=$(MODULE_PATH) --with-compat

nginx/:
	git clone --depth 1 --branch release-$(NGINX_VERSION) https://github.com/nginx/nginx
