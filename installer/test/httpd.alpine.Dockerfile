# Not in working order, as released version isn't built with musl
FROM httpd:2.4.62-alpine

RUN apk add --update --no-cache gnupg curl build-base libc6-compat gcc gcompat libstdc++
