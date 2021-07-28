ngx_http_opentracing_module.so: ../.build/libngx_http_opentracing_module.so
	cp $< $@
	
.PHONY: up
up: ngx_http_opentracing_module.so
	docker-compose up --build
