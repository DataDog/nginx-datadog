FROM nginx:bookworm

RUN apt update && apt install -y gpg
