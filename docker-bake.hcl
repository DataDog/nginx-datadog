# docker-bake.hcl - Build configuration for nginx-datadog module
#
# The toolchain image (nginx_musl_toolchain) is built automatically from build_env/
# as part of the bake process. To use a pre-built external image instead, set:
#   TOOLCHAIN_IMAGE=public.ecr.aws/b1o7r7e0/nginx_musl_toolchain:latest docker buildx bake dev
#
# Usage examples:
#   docker buildx bake nginx-all              # Build all nginx versions
#   docker buildx bake openresty-all          # Build all openresty versions
#   docker buildx bake ingress-nginx-all      # Build all ingress-nginx versions
#   docker buildx bake all                    # Build everything
#   docker buildx bake dev                    # Minimal set for testing
#   docker buildx bake --print all            # List all targets
#
# SSI package builds:
#   docker buildx bake ssi                    # Build all modules for SSI package
#   docker buildx bake ssi-dev                # Build dev subset for SSI package
#
# Build specific targets:
#   docker buildx bake nginx-1-28-1-amd64-waf-ON
#   docker buildx bake nginx-1-29-4-arm64-waf-OFF
#   docker buildx bake openresty-1-27-1-2-amd64-waf-ON
#   docker buildx bake ingress-nginx-1-14-1-amd64
#
# Build just the toolchain:
#   docker buildx bake toolchain-amd64
#   docker buildx bake toolchain-arm64
#
# Override simple variables:
#   OUTPUT_DIR=/tmp/out docker buildx bake dev
#   docker buildx bake --set '*.args.MAKE_JOB_COUNT=16' dev
#
# Override version lists (create an override file):
#   echo 'NGINX_VERSIONS = ["1.28.1", "1.29.4"]' > override.hcl
#   docker buildx bake -f docker-bake.hcl -f override.hcl nginx-all

# =============================================================================
# Variables (all configurable via environment variables)
# =============================================================================

variable "NGINX_VERSIONS" {
  default = [
    "1.24.0",
    "1.26.0",
    "1.26.1",
    "1.26.2",
    "1.26.3",
    "1.27.0",
    "1.27.1",
    "1.27.2",
    "1.27.3",
    "1.27.4",
    "1.27.5",
    "1.28.0",
    "1.28.1",
    "1.29.0",
    "1.29.1",
    "1.29.2",
    "1.29.3",
    "1.29.4"
  ]
}

variable "OPENRESTY_VERSIONS" {
  default = [
    "1.25.3.1",
    "1.25.3.2",
    "1.27.1.1",
    "1.27.1.2"
  ]
}

variable "INGRESS_NGINX_VERSIONS" {
  default = [
    "1.10.0",
    "1.10.1",
    "1.10.2",
    "1.10.3",
    "1.10.4",
    "1.10.5",
    "1.10.6",
    "1.11.0",
    "1.11.1",
    "1.11.2",
    "1.11.3",
    "1.11.4",
    "1.11.5",
    "1.11.6",
    "1.11.7",
    "1.11.8",
    "1.12.0",
    "1.12.1",
    "1.12.2",
    "1.12.3",
    "1.12.4",
    "1.12.5",
    "1.12.6",
    "1.12.7",
    "1.12.8",
    "1.13.0",
    "1.13.1",
    "1.13.2",
    "1.13.3",
    "1.13.4",
    "1.13.5",
    "1.14.0",
    "1.14.1"
  ]
}

# =============================================================================
# SSI dev version subset (only latest stable nginx versions)
# =============================================================================

variable "NGINX_VERSIONS_SSI_DEV" {
  default = [
    "1.28.1",   # Latest stable
    "1.29.4"    # Latest mainline
  ]
}

variable "ARCHITECTURES" {
  default = ["amd64", "arm64"]
}

variable "WAF_OPTIONS" {
  default = ["ON", "OFF"]
}

variable "OUTPUT_DIR" {
  default = "./artifacts"
}

variable "SSI_IMAGE_REPO" {
  default = "ghcr.io/datadog/nginx-datadog-ssi"
}

variable "SSI_VERSION" {
  default = "dev"
}

variable "TOOLCHAIN_IMAGE" {
  # When empty, the toolchain will be built from build_env/Dockerfile
  # Set to an image name to use a pre-built toolchain (e.g., "public.ecr.aws/b1o7r7e0/nginx_musl_toolchain:latest")
  default = ""
}

variable "BUILD_TYPE" {
  default = "RelWithDebInfo"
}

variable "MAKE_JOB_COUNT" {
  default = "8"
}

# =============================================================================
# Inject Browser SDK Targets (for RUM builds)
# =============================================================================
# Build FFI artifacts from the inject-browser-sdk submodule

target "inject-browser-sdk" {
  name       = "inject-browser-sdk-${arch}"
  dockerfile = "Dockerfile.artifacts"
  context    = "inject-browser-sdk"
  platforms  = ["linux/${arch}"]
  target     = "artifacts"

  matrix = {
    arch = ARCHITECTURES
  }

  tags = ["inject-browser-sdk-artifacts:${arch}"]
}

# =============================================================================
# Helper functions
# =============================================================================

# Convert arch name to toolchain ARCH value (amd64 -> x86_64, arm64 -> aarch64)
function "arch_to_toolchain" {
  params = [arch]
  result = arch == "amd64" ? "x86_64" : "aarch64"
}

# Convert version with dots to dashes for target names (1.28.1 -> 1-28-1)
function "version_to_name" {
  params = [version]
  result = replace(version, ".", "-")
}

# Convert WAF option to directory name (ON -> waf, OFF -> no-waf)
function "waf_to_dir" {
  params = [waf]
  result = waf == "ON" ? "waf" : "no-waf"
}

# =============================================================================
# Toolchain Targets
# =============================================================================

# Build the musl toolchain from build_env/Dockerfile
target "toolchain" {
  name       = "toolchain-${arch}"
  dockerfile = "Dockerfile"
  context    = "build_env"
  platforms  = ["linux/${arch}"]

  matrix = {
    arch = ARCHITECTURES
  }

  args = {
    ARCH = arch_to_toolchain(arch)
  }

  tags = ["nginx_musl_toolchain:${arch}"]
}

# =============================================================================
# Target Groups
# =============================================================================

group "default" {
  targets = ["nginx-all"]
}

group "all" {
  targets = ["nginx-all", "openresty-all", "ingress-nginx-all"]
}

group "nginx-all" {
  targets = ["nginx"]
}

group "openresty-all" {
  targets = ["openresty"]
}

group "ingress-nginx-all" {
  targets = ["ingress-nginx"]
}

# Minimal dev build for testing (latest nginx, amd64 only, WAF ON)
group "dev" {
  targets = ["nginx-dev", "openresty-dev", "ingress-nginx-dev"]
}

# =============================================================================
# SSI Package Groups
# =============================================================================

# Full SSI build - nginx only with RUM injection enabled
# Use this for release builds
group "ssi" {
  targets = ["ssi-nginx"]
}

# Dev SSI build - subset of nginx versions for quick testing
group "ssi-dev" {
  targets = ["ssi-nginx-dev"]
}

# =============================================================================
# Nginx Targets
# =============================================================================

target "nginx" {
  name       = "nginx-${version_to_name(version)}-${arch}-waf-${waf}"
  dockerfile = "packaging/Dockerfile.nginx"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    version = NGINX_VERSIONS
    arch    = ARCHITECTURES
    waf     = WAF_OPTIONS
  }

  args = {
    ARCH            = arch_to_toolchain(arch)
    NGINX_VERSION   = version
    WAF             = waf
    BUILD_TYPE      = BUILD_TYPE
    MAKE_JOB_COUNT  = MAKE_JOB_COUNT
  }

  # Use locally built toolchain or external image
  contexts = {
    toolchain = TOOLCHAIN_IMAGE == "" ? "target:toolchain-${arch}" : "docker-image://${TOOLCHAIN_IMAGE}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/nginx/${version}/${arch}/${waf_to_dir(waf)}"]
  target = "export"
}

# Dev target for quick testing
target "nginx-dev" {
  dockerfile = "packaging/Dockerfile.nginx"
  context    = "."
  platforms  = ["linux/amd64"]

  args = {
    ARCH            = "x86_64"
    NGINX_VERSION   = "1.29.4"
    WAF             = "ON"
    BUILD_TYPE      = BUILD_TYPE
    MAKE_JOB_COUNT  = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain = TOOLCHAIN_IMAGE == "" ? "target:toolchain-amd64" : "docker-image://${TOOLCHAIN_IMAGE}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/nginx/1.29.4/amd64/waf"]
  target = "export"
}

# =============================================================================
# OpenResty Targets
# =============================================================================

target "openresty" {
  name       = "openresty-${version_to_name(version)}-${arch}-waf-${waf}"
  dockerfile = "packaging/Dockerfile.openresty"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    version = OPENRESTY_VERSIONS
    arch    = ARCHITECTURES
    waf     = WAF_OPTIONS
  }

  args = {
    ARCH            = arch_to_toolchain(arch)
    RESTY_VERSION   = version
    WAF             = waf
    BUILD_TYPE      = BUILD_TYPE
    MAKE_JOB_COUNT  = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain = TOOLCHAIN_IMAGE == "" ? "target:toolchain-${arch}" : "docker-image://${TOOLCHAIN_IMAGE}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/openresty/${version}/${arch}/${waf_to_dir(waf)}"]
  target = "export"
}

# Dev target for quick testing
target "openresty-dev" {
  dockerfile = "packaging/Dockerfile.openresty"
  context    = "."
  platforms  = ["linux/amd64"]

  args = {
    ARCH            = "x86_64"
    RESTY_VERSION   = "1.27.1.2"
    WAF             = "ON"
    BUILD_TYPE      = BUILD_TYPE
    MAKE_JOB_COUNT  = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain = TOOLCHAIN_IMAGE == "" ? "target:toolchain-amd64" : "docker-image://${TOOLCHAIN_IMAGE}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/openresty/1.27.1.2/amd64/waf"]
  target = "export"
}

# =============================================================================
# Ingress-nginx Targets (WAF always ON)
# =============================================================================

target "ingress-nginx" {
  name       = "ingress-nginx-${version_to_name(version)}-${arch}"
  dockerfile = "packaging/Dockerfile.ingress"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    version = INGRESS_NGINX_VERSIONS
    arch    = ARCHITECTURES
  }

  args = {
    ARCH                 = arch_to_toolchain(arch)
    INGRESS_NGINX_VERSION = version
    WAF                  = "ON"
    BUILD_TYPE           = BUILD_TYPE
    MAKE_JOB_COUNT       = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain = TOOLCHAIN_IMAGE == "" ? "target:toolchain-${arch}" : "docker-image://${TOOLCHAIN_IMAGE}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/ingress-nginx/${version}/${arch}"]
  target = "export"
}

# Dev target for quick testing
target "ingress-nginx-dev" {
  dockerfile = "packaging/Dockerfile.ingress"
  context    = "."
  platforms  = ["linux/amd64"]

  args = {
    ARCH                 = "x86_64"
    INGRESS_NGINX_VERSION = "1.14.1"
    WAF                  = "ON"
    BUILD_TYPE           = BUILD_TYPE
    MAKE_JOB_COUNT       = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain = TOOLCHAIN_IMAGE == "" ? "target:toolchain-amd64" : "docker-image://${TOOLCHAIN_IMAGE}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/ingress-nginx/1.14.1/amd64"]
  target = "export"
}

# =============================================================================
# SSI Package Targets
# =============================================================================
# These targets build nginx modules for SSI packages with RUM injection enabled.
# Note: RUM and WAF are mutually exclusive - SSI uses RUM only.

# SSI Nginx - all versions, both architectures, RUM ON
target "ssi-nginx" {
  name       = "ssi-nginx-${version_to_name(version)}-${arch}"
  dockerfile = "packaging/Dockerfile.nginx"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    version = NGINX_VERSIONS
    arch    = ARCHITECTURES
  }

  args = {
    ARCH                       = arch_to_toolchain(arch)
    NGINX_VERSION              = version
    WAF                        = "OFF"
    RUM                        = "ON"
    BUILD_TYPE                 = BUILD_TYPE
    MAKE_JOB_COUNT             = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain                  = TOOLCHAIN_IMAGE == "" ? "target:toolchain-${arch}" : "docker-image://${TOOLCHAIN_IMAGE}"
    inject-browser-sdk-artifacts = "target:inject-browser-sdk-${arch}"
  }

  tags   = ["${SSI_IMAGE_REPO}:${SSI_VERSION}-nginx-${version}-${arch}"]
  output = ["type=docker"]
  target = "export"
}

# SSI Nginx Dev - subset of versions for quick testing
target "ssi-nginx-dev" {
  name       = "ssi-nginx-dev-${version_to_name(version)}-${arch}"
  dockerfile = "packaging/Dockerfile.nginx"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    version = NGINX_VERSIONS_SSI_DEV
    arch    = ARCHITECTURES
  }

  args = {
    ARCH                       = arch_to_toolchain(arch)
    NGINX_VERSION              = version
    WAF                        = "OFF"
    RUM                        = "ON"
    BUILD_TYPE                 = BUILD_TYPE
    MAKE_JOB_COUNT             = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain                  = TOOLCHAIN_IMAGE == "" ? "target:toolchain-${arch}" : "docker-image://${TOOLCHAIN_IMAGE}"
    inject-browser-sdk-artifacts = "target:inject-browser-sdk-${arch}"
  }

  tags   = ["${SSI_IMAGE_REPO}:${SSI_VERSION}-nginx-${version}-${arch}"]
  output = ["type=docker"]
  target = "export"
}
