# Agents Guide

All public Docker images must use `registry.ddbuild.io` mirrors. Managed by `bin/mirror_images.py` (subcommands: `add`, `lock`, `relock`, `mirror`, `lint`). Config: `mirror_images.yaml` / `mirror_images.lock.yaml`. Mirror prefix: `registry.ddbuild.io/ci/nginx-datadog/mirror/`. In `build_env/Dockerfile`, `MIRROR_REGISTRY` ARG is set to `""` in GHA to use public registries.
