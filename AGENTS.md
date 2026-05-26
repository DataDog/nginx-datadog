# Agent Instructions

`make format` to format, `make lint` to check. Rebuild formatter image after editing `Dockerfile.formatter` with `make build-formatter-image`.

Public Docker images must use `registry.ddbuild.io` mirrors. Managed by `bin/mirror_images` (`add`, `lock`, `relock`, `mirror`, `lint`). Config: `mirror_images.yaml` / `mirror_images.lock.yaml`. In `build_env/Dockerfile`, `MIRROR_REGISTRY` ARG is `""` in GHA to use public registries.
