FROM amazonlinux:2023

RUN dnf -y install nginx tar
RUN dnf -y swap gnupg2-minimal gnupg2-full
