#!/bin/sh

# Print an nginx module `config` file to standard output.
# The command line argument is the name of the module.
cat <<END_CONFIG
ngx_addon_name=$1
ngx_module_type=HTTP
ngx_module_name="\$ngx_addon_name"

# Make sure that our module is listed _before_ any of the modules whose
# configuration directives we override.  This way, our module can define
# handlers for those directives that do some processing and then forward
# to the "real" handler in the other module.
ngx_module_order="\$ngx_addon_name ngx_http_proxy_module"
# TODO: if this works, explain the backwardness.
# ngx_module_order="ngx_http_proxy_module \$ngx_addon_name"

. auto/module
END_CONFIG
