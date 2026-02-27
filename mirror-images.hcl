variable "REGISTRY" {
  default = "registry.ddbuild.io/ci/nginx-datadog/tests/images"
}

group "default" {
  targets = ["nginx", "nginx-alpine", "openresty", "amazonlinux"]
}

# nginx — standard (debian-based) variants
target "nginx" {
  matrix = {
    version = [
      "1.24.0",
      "1.25.0", "1.25.1", "1.25.2", "1.25.3", "1.25.4", "1.25.5",
      "1.26.0", "1.26.1", "1.26.2", "1.26.3",
      "1.27.0", "1.27.1", "1.27.2", "1.27.3", "1.27.4", "1.27.5",
      "1.28.0", "1.28.1", "1.28.2",
      "1.29.0", "1.29.1", "1.29.2", "1.29.3", "1.29.4", "1.29.5",
    ]
  }
  name              = "nginx-${replace(version, ".", "-")}"
  dockerfile-inline = "FROM nginx:${version}"
  tags              = ["${REGISTRY}/nginx:${version}"]
  platforms         = ["linux/amd64", "linux/arm64"]
}

# nginx — alpine variants
target "nginx-alpine" {
  matrix = {
    version = [
      "1.24.0",
      "1.25.0", "1.25.1", "1.25.2", "1.25.3", "1.25.4", "1.25.5",
      "1.26.0", "1.26.1", "1.26.2", "1.26.3",
      "1.27.0", "1.27.1", "1.27.2", "1.27.3", "1.27.4", "1.27.5",
      "1.28.0", "1.28.1", "1.28.2",
      "1.29.0", "1.29.1", "1.29.2", "1.29.3", "1.29.4", "1.29.5",
    ]
  }
  name              = "nginx-${replace(version, ".", "-")}-alpine"
  dockerfile-inline = "FROM nginx:${version}-alpine"
  tags              = ["${REGISTRY}/nginx:${version}-alpine"]
  platforms         = ["linux/amd64", "linux/arm64"]
}

# OpenResty — alpine variants
target "openresty" {
  matrix = {
    version = ["1.25.3.1", "1.25.3.2", "1.27.1.1", "1.27.1.2"]
  }
  name              = "openresty-${replace(version, ".", "-")}"
  dockerfile-inline = "FROM openresty/openresty:${version}-alpine"
  tags              = ["${REGISTRY}/openresty/openresty:${version}-alpine"]
  platforms         = ["linux/amd64", "linux/arm64"]
}

# Amazon Linux 2023
target "amazonlinux" {
  dockerfile-inline = "FROM amazonlinux:2023.3.20240219.0"
  tags              = ["${REGISTRY}/amazonlinux:2023.3.20240219.0"]
  platforms         = ["linux/amd64", "linux/arm64"]
}
