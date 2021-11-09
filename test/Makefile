ngx_http_datadog_module.so: ../.docker-build/libngx_http_datadog_module.so
# ngx_http_datadog_module.so: ../.build/libngx_http_datadog_module.so
	cp $< $@
	
.PHONY: up
up: ngx_http_datadog_module.so
	@if [ -z "$$DD_API_KEY" ]; then >&2 echo 'You forgot to set DD_API_KEY'; exit 1; fi
	bin/up
