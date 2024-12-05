FROM httpd:2.4.62-bookworm

RUN apt update && apt install -y gpg curl
