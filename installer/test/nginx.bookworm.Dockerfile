FROM nginx:1.27.1-bookworm

RUN apt update && apt install -y gpg
