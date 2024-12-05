# Not working because module requires glibc 3.4.30, also should be solved with musl
# Syntax error on line 1 of /opt/datadog-httpd/datadog.conf: Cannot load /opt/datadog-httpd/mod_datadog.so into server:
# /lib64/libstdc++.so.6: version `GLIBCXX_3.4.30' not found (required by /opt/datadog-httpd/mod_datadog.so)\n

FROM amazonlinux:2023

RUN dnf -y install httpd tar
RUN dnf -y swap gnupg2-minimal gnupg2-full
RUN dnf -y upgrade libstdc++
