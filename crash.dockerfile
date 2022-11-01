from openresty/openresty:1.21.4.1-3-alpine

run apk update && apk add gdb
cmd ["gdb", "--args", "nginx", "-T"]
