# Agent Instructions

## Code Formatting

```bash
make format  # format (builds Docker image if needed)
make lint    # check without modifying
```

Docker is used automatically for consistency. Skipped when already in Docker/CI.

- `make format-local` / `make lint-local` — use locally installed clang-format-14 and yapf3
- If you update `Dockerfile.formatter`, rebuild with `make build-formatter-image`
