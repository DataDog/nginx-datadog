# Agents Guide

## Docker Image Mirroring

All public Docker images must use `registry.ddbuild.io` mirrors — never reference Docker Hub, ghcr.io, or registry.k8s.io directly. Managed by `bin/mirror_images.py`:

```bash
uv run bin/mirror_images.py add 'nginx:1.30.0'       # add to mirror_images.yaml
uv run bin/mirror_images.py lock                       # resolve digests -> lock file
uv run bin/mirror_images.py relock 'nginx:1.29.*'     # re-resolve matching images
uv run bin/mirror_images.py mirror                     # push to registry
uv run bin/mirror_images.py lint                       # check for public refs
```

- Config: `mirror_images.yaml` / `mirror_images.lock.yaml`
- Mirror prefix: `registry.ddbuild.io/ci/nginx-datadog/mirror/`
- `build_env/Dockerfile` uses `MIRROR_REGISTRY` ARG (set to `""` in GHA to use public registries)

## Code Formatting

```bash
make format  # format (builds Docker image if needed)
make lint    # check without modifying
```

Docker is used automatically for consistency. Skipped when already in Docker/CI.

- If you update `Dockerfile.formatter`, rebuild with `make build-formatter-image`.
