# Agent Instructions

## Code Formatting

Code must be formatted using the Docker-based formatter before committing changes.

**First-time setup:**
```bash
make build-formatter-image
```

**Format your code:**
```bash
make format-docker
```

**Check formatting (without modifying files):**
```bash
make lint-docker
```

The formatter uses Docker to ensure consistent formatting across all environments without requiring local installation of clang-format-14 or yapf3.

---

## CI Integration

GitLab CI automatically builds and uses the formatter image for each pipeline.

### How It Works

Each pipeline:
1. **Builds** the formatter image from `Dockerfile.formatter` (tagged with pipeline ID)
2. **Pushes** both amd64 and arm64 images to `registry.ddbuild.io/ci/nginx-datadog/formatter:$CI_PIPELINE_ID`
3. **Uses** that pipeline-specific image in the `format` job

This ensures:
- ✅ Perfect consistency between local development and CI
- ✅ Automatic formatter image updates when `Dockerfile.formatter` changes
- ✅ No manual intervention required
- ✅ Isolated formatter versions per pipeline

### Updating the Formatter Image

**In CI**: No action needed! The formatter image is automatically rebuilt for each pipeline.

**For local development**: If you update `Dockerfile.formatter`, rebuild your local image:
```bash
make build-formatter-image
```

**Pushing to registry manually** (requires registry access):
```bash
# Tag with custom version
FORMATTER_TAG=v1.2.3 make build-push-formatter

# Or tag as latest
FORMATTER_TAG=latest make build-push-formatter
```
