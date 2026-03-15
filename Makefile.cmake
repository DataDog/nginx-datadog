# Multi-version build system for nginx modules.
# Included from the main Makefile.
#
# Stage 1 builds heavy deps (dd-trace-cpp, libddwaf) once.
# Stage 2 builds the module per nginx version using pre-built deps.

# Default: read all supported versions from the canonical list.
NGINX_VERSIONS ?= $(shell grep -v '^\#' nginx_versions.txt | grep -v '^\s*$$' | tr '\n' ' ')

DEPS_BUILD_DIR ?= .deps-build
NGINX_SOURCES_DIR ?= .nginx-sources
PARALLEL_VERSIONS ?= 4


# Force reconfigure by removing CMakeCache.txt for all versions.
.PHONY: reconfigure
reconfigure:
	rm -f $(foreach v,$(NGINX_VERSIONS),.build-$(v)/CMakeCache.txt)

# Stage 1: build shared dependencies once.
.PHONY: build-deps
build-deps: dd-trace-cpp-deps
	cmake -B $(DEPS_BUILD_DIR) \
		-DNGINX_DATADOG_ASM_ENABLED=$(WAF) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		$(CMAKE_PCRE_OPTIONS) \
		cmake/deps-only \
		&& cmake --build $(DEPS_BUILD_DIR) -j $(MAKE_JOB_COUNT) -v

# Stage 1 (musl): build shared dependencies with the musl toolchain.
.PHONY: build-deps-musl
build-deps-musl: dd-trace-cpp-deps $(TOOLCHAIN_DEPENDENCY)
ifdef GITLAB_CI
	$(MAKE) build-deps-musl-aux
else
	docker run --init --rm \
		--platform $(DOCKER_PLATFORM) \
		--env ARCH=$(ARCH) \
		--env BUILD_TYPE=$(BUILD_TYPE) \
		--env WAF=$(WAF) \
		--env RUM=$(RUM) \
		--mount "type=bind,source=$(dir $(lastword $(MAKEFILE_LIST))),destination=/mnt/repo" \
		$(BUILD_IMAGE) \
		make -C /mnt/repo build-deps-musl-aux
endif

.PHONY: build-deps-musl-aux
build-deps-musl-aux:
	cmake -B $(DEPS_BUILD_DIR) \
		-DCMAKE_TOOLCHAIN_FILE=/sysroot/$(ARCH)-none-linux-musl/Toolchain.cmake \
		-DNGINX_DATADOG_ASM_ENABLED=$(WAF) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		cmake/deps-only \
		&& cmake --build $(DEPS_BUILD_DIR) -j $(MAKE_JOB_COUNT) -v

# Pre-configure nginx sources for all requested versions.
.PHONY: prepare-nginx-sources
prepare-nginx-sources:
	WAF=$(WAF) bin/prepare_nginx_sources.sh $(NGINX_SOURCES_DIR) $(NGINX_VERSIONS)

# Stage 2: build the module for a single nginx version using pre-built deps.
.PHONY: build-module-for-version
build-module-for-version:
ifndef NGINX_VERSION
	$(error NGINX_VERSION is not set)
endif
	@if [ -f .build-$(NGINX_VERSION)/CMakeCache.txt ] && \
	    grep -q 'NGINX_DATADOG_ASM_ENABLED:BOOL=$(WAF)' .build-$(NGINX_VERSION)/CMakeCache.txt && \
	    grep -q 'NGINX_DATADOG_RUM_ENABLED:BOOL=$(RUM)' .build-$(NGINX_VERSION)/CMakeCache.txt && \
	    grep -q 'CMAKE_BUILD_TYPE:STRING=$(BUILD_TYPE)' .build-$(NGINX_VERSION)/CMakeCache.txt; then \
		echo "skipping configure for nginx $(NGINX_VERSION)"; \
	else \
		cmake -B .build-$(NGINX_VERSION) \
			-DNGINX_SRC_DIR=$(NGINX_SOURCES_DIR)/$(NGINX_VERSION) \
			-DDEPS_BUILD_DIR=$(DEPS_BUILD_DIR) \
			-DNGINX_DATADOG_ASM_ENABLED=$(WAF) \
			-DNGINX_DATADOG_RUM_ENABLED=$(RUM) \
			-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
			-DBUILD_TESTING=OFF \
			$(CMAKE_PCRE_OPTIONS) . ; \
	fi
	cmake --build .build-$(NGINX_VERSION) -j $(MAKE_JOB_COUNT) -v --target ngx_http_datadog_module
	chmod 755 .build-$(NGINX_VERSION)/ngx_http_datadog_module.so
	@echo "built module for nginx $(NGINX_VERSION)"

# Stage 2 (musl): build the module for a single version with musl toolchain + pre-built deps.
.PHONY: build-module-for-version-musl
build-module-for-version-musl:
ifndef NGINX_VERSION
	$(error NGINX_VERSION is not set)
endif
	@if [ -f .build-$(NGINX_VERSION)/CMakeCache.txt ] && \
	    grep -q 'NGINX_DATADOG_ASM_ENABLED:BOOL=$(WAF)' .build-$(NGINX_VERSION)/CMakeCache.txt && \
	    grep -q 'NGINX_DATADOG_RUM_ENABLED:BOOL=$(RUM)' .build-$(NGINX_VERSION)/CMakeCache.txt && \
	    grep -q 'CMAKE_BUILD_TYPE:STRING=$(BUILD_TYPE)' .build-$(NGINX_VERSION)/CMakeCache.txt; then \
		echo "skipping configure for nginx $(NGINX_VERSION)"; \
	else \
		cmake -B .build-$(NGINX_VERSION) \
			-DCMAKE_TOOLCHAIN_FILE=/sysroot/$(ARCH)-none-linux-musl/Toolchain.cmake \
			-DNGINX_PATCH_AWAY_LIBC=ON \
			-DNGINX_SRC_DIR=$(NGINX_SOURCES_DIR)/$(NGINX_VERSION) \
			-DDEPS_BUILD_DIR=$(DEPS_BUILD_DIR) \
			-DNGINX_DATADOG_ASM_ENABLED=$(WAF) \
			-DNGINX_DATADOG_RUM_ENABLED=$(RUM) \
			-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
			-DBUILD_TESTING=OFF . ; \
	fi
	cmake --build .build-$(NGINX_VERSION) -j $(MAKE_JOB_COUNT) -v --target ngx_http_datadog_module

# Build all nginx versions in parallel using pre-built deps.
.PHONY: build-all-versions
build-all-versions: build-deps prepare-nginx-sources
	echo $(NGINX_VERSIONS) | tr ' ' '\n' | \
		xargs -P $(PARALLEL_VERSIONS) -I{} \
		$(MAKE) build-module-for-version NGINX_VERSION={}
	@echo "all versions built successfully"

# Build all nginx versions (musl) in parallel using pre-built deps.
.PHONY: build-all-versions-musl
build-all-versions-musl: $(TOOLCHAIN_DEPENDENCY)
ifdef GITLAB_CI
	$(MAKE) build-all-versions-musl-aux
else
	docker run --init --rm \
		--platform $(DOCKER_PLATFORM) \
		--env ARCH=$(ARCH) \
		--env BUILD_TYPE=$(BUILD_TYPE) \
		--env WAF=$(WAF) \
		--env RUM=$(RUM) \
		--env NGINX_VERSIONS="$(NGINX_VERSIONS)" \
		--env PARALLEL_VERSIONS=$(PARALLEL_VERSIONS) \
		--mount "type=bind,source=$(dir $(lastword $(MAKEFILE_LIST))),destination=/mnt/repo" \
		$(BUILD_IMAGE) \
		make -C /mnt/repo build-all-versions-musl-aux
endif

.PHONY: build-all-versions-musl-aux
build-all-versions-musl-aux: build-deps-musl-aux prepare-nginx-sources
	echo $(NGINX_VERSIONS) | tr ' ' '\n' | \
		xargs -P $(PARALLEL_VERSIONS) -I{} \
		$(MAKE) build-module-for-version-musl NGINX_VERSION={}
	@echo "all musl versions built successfully"
