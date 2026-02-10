# docker-bake.hcl - Build configuration for nginx-datadog module
#
# The toolchain image (nginx_musl_toolchain) is built automatically from build_env/
# as part of the bake process. To use a pre-built external image instead, set:
#   TOOLCHAIN_IMAGE=public.ecr.aws/b1o7r7e0/nginx_musl_toolchain:latest docker buildx bake dev
#
# Usage examples:
#   docker buildx bake nginx-all              # Build all nginx versions
#   docker buildx bake all                    # Build everything
#   docker buildx bake dev                    # Minimal set for testing
#   docker buildx bake --print all            # List all targets
#
# SSI package builds:
#   docker buildx bake ssi                    # Build all modules for SSI package
#   docker buildx bake ssi-dev                # Build dev subset for SSI package
#
# SSI OCI package creation (builds modules automatically via dependencies):
#   ./bin/generate-ssi-oci-package-dev.sh 1.0.0   # Dev build (1.28.1 + 1.29.4 only)
#   ./bin/generate-ssi-oci-package.sh 1.0.0       # Full build (all versions)
#
# Or manually:
#   SSI_PACKAGE_VERSION=1.0.0 docker buildx bake ssi-package-dev        # Step 1: create
#   SSI_PACKAGE_VERSION=1.0.0 docker buildx bake ssi-package-merge-dev  # Step 2: merge
#
# Build specific targets:
#   docker buildx bake nginx-1-28-1-amd64
#   docker buildx bake nginx-1-29-4-arm64
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

variable "OUTPUT_DIR" {
  default = "./artifacts"
}

variable "SSI_IMAGE_REPO" {
  default = "ghcr.io/datadog/nginx-datadog-ssi"
}

variable "SSI_VERSION" {
  default = "dev"
}

variable "PUSH" {
  default = false
}

variable "SSI_PACKAGE_NAME" {
  default = "datadog-nginx-ssi"
}

variable "SSI_PACKAGE_VERSION" {
  default = "0.0.1-dev"
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
# Build FFI artifacts from inject-browser-sdk

target "inject-browser-sdk" {
  name       = "inject-browser-sdk-${arch}"
  dockerfile = "Dockerfile.artifacts"
  context    = "https://github.com/DataDog/inject-browser-sdk.git#cmake-corrosion-wrapper-external"
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
  targets = ["nginx-all"]
}

group "nginx-all" {
  targets = ["nginx"]
}

# Minimal dev build for testing (latest nginx, amd64 only)
group "dev" {
  targets = ["nginx-dev"]
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

# SSI OCI package creation - creates per-arch packages
# Run ssi-package-merge separately after this completes
group "ssi-package" {
  targets = ["ssi-package-create"]
}

# Dev SSI OCI package creation - creates per-arch packages
# Run ssi-package-merge-dev separately after this completes
group "ssi-package-dev" {
  targets = ["ssi-package-create-dev"]
}

# =============================================================================
# Nginx Targets
# =============================================================================

target "nginx" {
  name       = "nginx-${version_to_name(version)}-${arch}"
  dockerfile = "packaging/Dockerfile.nginx"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    version = NGINX_VERSIONS
    arch    = ARCHITECTURES
  }

  args = {
    ARCH            = arch_to_toolchain(arch)
    NGINX_VERSION   = version
    BUILD_TYPE      = BUILD_TYPE
    MAKE_JOB_COUNT  = MAKE_JOB_COUNT
  }

  # Use locally built toolchain or external image
  contexts = {
    toolchain = TOOLCHAIN_IMAGE == "" ? "target:toolchain-${arch}" : "docker-image://${TOOLCHAIN_IMAGE}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/nginx/${version}/${arch}"]
  target = "export"
}

# Dev target for quick testing
target "nginx-dev" {
  dockerfile = "packaging/Dockerfile.nginx"
  context    = "."
  platforms  = ["linux/amd64"]

  args = {
    ARCH            = "x86_64"
    NGINX_VERSION   = "1.28.0"
    BUILD_TYPE      = BUILD_TYPE
    MAKE_JOB_COUNT  = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain = TOOLCHAIN_IMAGE == "" ? "target:toolchain-amd64" : "docker-image://${TOOLCHAIN_IMAGE}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/nginx/1.28.0/amd64"]
  target = "export"
}

# =============================================================================
# SSI Package Targets
# =============================================================================
# These targets build nginx modules for SSI packages with RUM injection enabled.

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
    RUM                        = "ON"
    BUILD_TYPE                 = BUILD_TYPE
    MAKE_JOB_COUNT             = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain                  = TOOLCHAIN_IMAGE == "" ? "target:toolchain-${arch}" : "docker-image://${TOOLCHAIN_IMAGE}"
    inject-browser-sdk-artifacts = "target:inject-browser-sdk-${arch}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/nginx/${version}/${arch}/rum"]
  target = "export"
}

# SSI Nginx Dev - subset of versions for quick testing
# Outputs both to local filesystem and as Docker image for chaining
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
    RUM                        = "ON"
    BUILD_TYPE                 = BUILD_TYPE
    MAKE_JOB_COUNT             = MAKE_JOB_COUNT
  }

  contexts = {
    toolchain                  = TOOLCHAIN_IMAGE == "" ? "target:toolchain-${arch}" : "docker-image://${TOOLCHAIN_IMAGE}"
    inject-browser-sdk-artifacts = "target:inject-browser-sdk-${arch}"
  }

  tags   = ["ssi-nginx:${version}-${arch}"]
  output = ["type=local,dest=${OUTPUT_DIR}/nginx/${version}/${arch}/rum"]
  target = "export"
}

# =============================================================================
# SSI OCI Package Targets
# =============================================================================
# These targets create OCI packages for the SSI nginx modules using datadog-packages.

# Collect all nginx modules into a single image (full)
# Uses dockerfile-inline to dynamically generate COPY statements from NGINX_VERSIONS
target "ssi-nginx-modules" {
  name       = "ssi-nginx-modules-${arch}"
  platforms  = ["linux/${arch}"]

  matrix = {
    arch = ARCHITECTURES
  }

  dockerfile-inline = join("\n", concat(
    ["FROM scratch"],
    [for v in NGINX_VERSIONS : "COPY --from=nginx-${replace(v, ".", "-")} /ngx_http_datadog_module.so /nginx/${v}/ngx_http_datadog_module.so"]
  ))

  contexts = merge(
    {for v in NGINX_VERSIONS : "nginx-${replace(v, ".", "-")}" => "target:ssi-nginx-${replace(v, ".", "-")}-${arch}"}
  )

  tags = ["ssi-nginx-modules:${arch}"]
}

# Collect nginx modules into a single image (dev subset)
target "ssi-nginx-modules-dev" {
  name       = "ssi-nginx-modules-dev-${arch}"
  platforms  = ["linux/${arch}"]

  matrix = {
    arch = ARCHITECTURES
  }

  dockerfile-inline = join("\n", concat(
    ["FROM scratch"],
    [for v in NGINX_VERSIONS_SSI_DEV : "COPY --from=nginx-${replace(v, ".", "-")} /ngx_http_datadog_module.so /nginx/${v}/ngx_http_datadog_module.so"]
  ))

  contexts = merge(
    {for v in NGINX_VERSIONS_SSI_DEV : "nginx-${replace(v, ".", "-")}" => "target:ssi-nginx-dev-${replace(v, ".", "-")}-${arch}"}
  )

  tags = ["ssi-nginx-modules-dev:${arch}"]
}

# Assemble SSI sources - adds version file to collected modules (full)
target "ssi-package-assemble" {
  name       = "ssi-package-assemble-${arch}"
  dockerfile = "packaging/Dockerfile.ssi-sources"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    arch = ARCHITECTURES
  }

  args = {
    SSI_PACKAGE_VERSION = SSI_PACKAGE_VERSION
  }

  contexts = {
    nginx-modules = "target:ssi-nginx-modules-${arch}"
  }

  tags   = ["ssi-sources:${arch}"]
  target = "export"
}

# Assemble SSI sources (dev) - adds version file to collected modules
target "ssi-package-assemble-dev" {
  name       = "ssi-package-assemble-dev-${arch}"
  dockerfile = "packaging/Dockerfile.ssi-sources"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    arch = ARCHITECTURES
  }

  args = {
    SSI_PACKAGE_VERSION = SSI_PACKAGE_VERSION
  }

  contexts = {
    nginx-modules = "target:ssi-nginx-modules-dev-${arch}"
  }

  tags   = ["ssi-sources:${arch}"]
  target = "export"
}

# Create per-arch OCI packages (full)
target "ssi-package-create" {
  name       = "ssi-package-create-${arch}"
  dockerfile = "packaging/Dockerfile.ssi-package"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    arch = ARCHITECTURES
  }

  args = {
    PACKAGE_NAME = SSI_PACKAGE_NAME
    VERSION      = SSI_PACKAGE_VERSION
  }

  contexts = {
    sources = "target:ssi-package-assemble-${arch}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/ssi-packages"]
  target = "package-export"
}

# Create per-arch OCI packages (dev)
target "ssi-package-create-dev" {
  name       = "ssi-package-create-dev-${arch}"
  dockerfile = "packaging/Dockerfile.ssi-package"
  context    = "."
  platforms  = ["linux/${arch}"]

  matrix = {
    arch = ARCHITECTURES
  }

  args = {
    PACKAGE_NAME = SSI_PACKAGE_NAME
    VERSION      = SSI_PACKAGE_VERSION
  }

  contexts = {
    sources = "target:ssi-package-assemble-dev-${arch}"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/ssi-packages"]
  target = "package-export"
}

# Merge per-arch packages into multi-arch OCI index (full)
target "ssi-package-merge" {
  dockerfile = "packaging/Dockerfile.ssi-package"
  context    = "."
  platforms  = ["linux/amd64"]

  args = {
    PACKAGE_NAME = SSI_PACKAGE_NAME
    VERSION      = SSI_PACKAGE_VERSION
  }

  contexts = {
    packages = "${OUTPUT_DIR}/ssi-packages"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/ssi-packages"]
  target = "merge-export"
}

# Merge per-arch packages into multi-arch OCI index (dev)
target "ssi-package-merge-dev" {
  dockerfile = "packaging/Dockerfile.ssi-package"
  context    = "."
  platforms  = ["linux/amd64"]

  args = {
    PACKAGE_NAME = SSI_PACKAGE_NAME
    VERSION      = SSI_PACKAGE_VERSION
  }

  contexts = {
    packages = "${OUTPUT_DIR}/ssi-packages"
  }

  output = ["type=local,dest=${OUTPUT_DIR}/ssi-packages"]
  target = "merge-export"
}
