#!/bin/sh

# Print an nginx module `config` file to standard output.
# The command line argument is the name of the module.
cat <<END_CONFIG
ngx_addon_name=$1
ngx_module_type=HTTP
ngx_module_name="\$ngx_addon_name"

. auto/module
END_CONFIG
