#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyyaml"]
# ///
"""Generate .gitlab/build-and-test-all.yml and .gitlab/build-and-test-fast.yml
from supported_versions.yml.

Usage:
    uv run bin/generate_gitlab_ci.py           # write files
    uv run bin/generate_gitlab_ci.py --check   # compare and exit 1 on diff
"""

import argparse
import difflib
import os
import sys

try:
    import yaml
except ImportError:
    sys.exit(
        "PyYAML is required.  Install it with:\n"
        "    pip install pyyaml\n"
    )

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSIONS_FILE = os.path.join(REPO_ROOT, "supported_versions.yml")
ALL_FILE = os.path.join(REPO_ROOT, ".gitlab", "build-and-test-all.yml")
FAST_FILE = os.path.join(REPO_ROOT, ".gitlab", "build-and-test-fast.yml")

# Each nginx test matrix entry expands to 8 jobs: 2 arch * 2 base_image * 2 WAF
NGINX_TEST_JOBS_PER_VERSION = 8
# Extra test images expand to 4 jobs each: 2 arch * 1 base_image * 2 WAF
EXTRA_TEST_JOBS_PER_IMAGE = 4
GITLAB_MATRIX_LIMIT = 200


def version_sort_key(version_str):
    """Parse a version string like '1.27.5' or '1.25.3.1' into a tuple of ints
    for stable semver-style sorting."""
    return tuple(int(p) for p in version_str.split("."))


def load_versions():
    with open(VERSIONS_FILE) as f:
        return yaml.safe_load(f)


def all_nginx_versions(data):
    return sorted(
        (e["version"] for e in data["nginx"]["versions"]),
        key=version_sort_key,
    )


def fast_nginx_versions(data):
    return sorted(
        (e["version"] for e in data["nginx"]["versions"] if e.get("fast")),
        key=version_sort_key,
    )


def rum_nginx_versions(data):
    return sorted(
        (e["version"] for e in data["nginx"]["versions"] if e.get("rum")),
        key=version_sort_key,
    )


def all_ingress_versions(data):
    return sorted(
        (e["version"] for e in data["ingress_nginx"]["versions"]),
        key=version_sort_key,
    )


def fast_ingress_versions(data):
    return sorted(
        (e["version"] for e in data["ingress_nginx"]["versions"] if e.get("fast")),
        key=version_sort_key,
    )


def all_openresty_versions(data):
    return sorted(
        (e["version"] for e in data["openresty"]["versions"]),
        key=version_sort_key,
    )


def fast_openresty_versions(data):
    return sorted(
        (e["version"] for e in data["openresty"]["versions"] if e.get("fast")),
        key=version_sort_key,
    )


def extra_test_images(data):
    return sorted(
        data["nginx"].get("extra_test_images", []),
        key=lambda e: version_sort_key(e["nginx_version"]),
    )


def coverage_version(data):
    return data["nginx"]["coverage_version"]


# ---------------------------------------------------------------------------
# YAML formatting helpers — we emit strings, not yaml.dump(), to match style
# ---------------------------------------------------------------------------

def version_list_block(versions, indent=8):
    """Render a YAML block list of quoted version strings."""
    prefix = " " * indent
    return "\n".join(f'{prefix}- "{v}"' for v in versions)


def nginx_test_matrix_entries(versions, indent=6):
    """Render test-nginx matrix entries — one per version with BASE_IMAGE pair."""
    lines = []
    prefix = " " * indent
    for v in versions:
        lines.append(f'{prefix}- ARCH: ["amd64", "arm64"]')
        lines.append(f'{prefix}  BASE_IMAGE: ["nginx:{v}", "nginx:{v}-alpine"]')
        lines.append(f'{prefix}  NGINX_VERSION: ["{v}"]')
        lines.append(f'{prefix}  WAF: ["ON", "OFF"]')
    return "\n".join(lines)


def nginx_rum_test_matrix_entries(versions, indent=6):
    """Render test-nginx-rum matrix entries — one per version, WAF OFF only."""
    lines = []
    prefix = " " * indent
    for v in versions:
        lines.append(f'{prefix}- ARCH: ["amd64", "arm64"]')
        lines.append(f'{prefix}  BASE_IMAGE: ["nginx:{v}"]')
        lines.append(f'{prefix}  NGINX_VERSION: ["{v}"]')
        lines.append(f'{prefix}  WAF: ["OFF"]')
    return "\n".join(lines)


def extra_test_matrix_entries(images, indent=6):
    """Render extra test image matrix entries."""
    lines = []
    prefix = " " * indent
    for img in images:
        lines.append(f'{prefix}- ARCH: ["amd64", "arm64"]')
        lines.append(f'{prefix}  BASE_IMAGE: ["{img["base_image"]}"]')
        lines.append(f'{prefix}  NGINX_VERSION: ["{img["nginx_version"]}"]')
        lines.append(f'{prefix}  WAF: ["ON", "OFF"]')
    return "\n".join(lines)


def split_test_nginx_all(all_versions, extras):
    """Split nginx versions across test-nginx-all / test-nginx-all-bis to stay
    under the GitLab 200-job matrix limit.

    Returns (first_versions, first_extras, second_versions, second_extras).
    """
    total_jobs = 0
    first_versions = []
    for v in all_versions:
        total_jobs += NGINX_TEST_JOBS_PER_VERSION
        if total_jobs > GITLAB_MATRIX_LIMIT:
            break
        first_versions.append(v)

    second_versions = all_versions[len(first_versions):]

    # Try to fit extras in the first batch
    extra_jobs = len(extras) * EXTRA_TEST_JOBS_PER_IMAGE
    if total_jobs + extra_jobs <= GITLAB_MATRIX_LIMIT:
        first_extras = extras
        second_extras = []
    else:
        first_extras = []
        second_extras = extras

    return first_versions, first_extras, second_versions, second_extras


# ---------------------------------------------------------------------------
# File generators
# ---------------------------------------------------------------------------

def generate_all(data):
    nginx_versions = all_nginx_versions(data)
    ingress_versions = all_ingress_versions(data)
    openresty_versions = all_openresty_versions(data)
    rum_versions = rum_nginx_versions(data)
    extras = extra_test_images(data)

    first_v, first_extras, second_v, second_extras = split_test_nginx_all(
        nginx_versions, extras
    )

    lines = []
    lines.append("include:")
    lines.append('  - local: ".gitlab/common.yml"')
    lines.append("")
    lines.append("# We split build and test all in two stages because GitLab is not able to handle dependencies graph")
    lines.append("# with more than 100 jobs!")
    lines.append("")
    lines.append(".build-and-test-all:")
    lines.append("  rules:")
    lines.append('    - if: $CI_COMMIT_TAG =~ /^v[0-9]+\\.[0-9]+\\.[0-9]+/')
    lines.append("      when: always")
    lines.append("    - when: never")
    lines.append("")
    lines.append(".build-all:")
    lines.append("  extends: .build-and-test-all")
    lines.append("  stage: build-all")
    lines.append("")
    lines.append(".test-all:")
    lines.append("  extends: .build-and-test-all")
    lines.append("  stage: test-all")
    lines.append("")

    # build-nginx-all
    lines.append("build-nginx-all:")
    lines.append("  extends:")
    lines.append("    - .build-all")
    lines.append("    - .build-nginx")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        NGINX_VERSION:")
    lines.append(version_list_block(nginx_versions, indent=10))
    lines.append('        WAF: ["ON", "OFF"]')
    lines.append("")

    # build-nginx-rum-all
    lines.append("build-nginx-rum-all:")
    lines.append("  extends:")
    lines.append("    - .build-all")
    lines.append("    - .build-nginx-rum")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        NGINX_VERSION:")
    lines.append(version_list_block(rum_versions, indent=10))
    lines.append('        WAF: ["OFF"]')
    lines.append("")

    # build-ingress-nginx-all
    lines.append("build-ingress-nginx-all:")
    lines.append("  extends:")
    lines.append("    - .build-all")
    lines.append("    - .build-ingress-nginx")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        INGRESS_NGINX_VERSION:")
    lines.append(version_list_block(ingress_versions, indent=10))
    lines.append("")

    # build-openresty-all
    lines.append("build-openresty-all:")
    lines.append("  extends:")
    lines.append("    - .build-all")
    lines.append("    - .build-openresty")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        RESTY_VERSION:")
    lines.append(version_list_block(openresty_versions, indent=10))
    lines.append('        WAF: ["ON", "OFF"]')
    lines.append("")

    # test-nginx-all
    lines.append("test-nginx-all:")
    lines.append("  extends:")
    lines.append("    - .test-all")
    lines.append("    - .test-nginx")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append(nginx_test_matrix_entries(first_v))
    if first_extras:
        lines.append(extra_test_matrix_entries(first_extras))
    lines.append("")

    # test-nginx-all-bis
    lines.append("# GitLab is not able to handle a matrix with more than 200 jobs!")
    lines.append("test-nginx-all-bis:")
    lines.append("  extends:")
    lines.append("    - .test-all")
    lines.append("    - .test-nginx")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append(nginx_test_matrix_entries(second_v))
    if second_extras:
        lines.append(extra_test_matrix_entries(second_extras))
    lines.append("")

    # test-nginx-rum-all
    lines.append("test-nginx-rum-all:")
    lines.append("  extends:")
    lines.append("    - .test-all")
    lines.append("    - .test-nginx-rum")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append(nginx_rum_test_matrix_entries(rum_versions))
    lines.append("")

    # test-ingress-nginx-all
    lines.append("test-ingress-nginx-all:")
    lines.append("  extends:")
    lines.append("    - .test-all")
    lines.append("    - .test-ingress-nginx")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        INGRESS_NGINX_VERSION:")
    lines.append(version_list_block(ingress_versions, indent=10))
    lines.append("")

    # test-openresty-all
    lines.append("test-openresty-all:")
    lines.append("  extends:")
    lines.append("    - .test-all")
    lines.append("    - .test-openresty")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        RESTY_VERSION:")
    lines.append(version_list_block(openresty_versions, indent=10))
    lines.append('        WAF: ["ON", "OFF"]')
    lines.append("")

    return "\n".join(lines)


def generate_fast(data):
    fast_nginx = fast_nginx_versions(data)
    rum_versions = rum_nginx_versions(data)
    fast_ingress = fast_ingress_versions(data)
    fast_openresty = fast_openresty_versions(data)
    extras = extra_test_images(data)
    cov_version = coverage_version(data)

    lines = []
    lines.append("include:")
    lines.append('  - local: ".gitlab/common.yml"')
    lines.append("")
    lines.append(".build-and-test-fast:")
    lines.append("  stage: build-and-test-fast")
    lines.append("  rules:")
    lines.append("    - if: $CI_COMMIT_TAG")
    lines.append("      when: never")
    lines.append("    - when: always")
    lines.append("")

    # build-formatter-image
    lines.append("build-formatter-image:")
    lines.append("  extends: .build-and-test-fast")
    lines.append("  image: registry.ddbuild.io/images/nydus:v2.3.8-dd")
    lines.append('  tags: ["docker-in-docker:amd64"]')
    lines.append("  script:")
    lines.append("    - |")
    lines.append("      HASH=$(cat Dockerfile.formatter | sha256sum | cut -d ' ' -f 1)")
    lines.append('    - echo "FORMATTER_IMAGE_TAG=$CI_REGISTRY/formatter:$HASH" >> formatter-image.env')
    lines.append("    - |")
    lines.append('      if docker buildx imagetools inspect "$CI_REGISTRY/formatter:$HASH" > /dev/null 2>&1; then')
    lines.append('        echo "Image $CI_REGISTRY/formatter:$HASH already exists. Skipping build."')
    lines.append("      else")
    lines.append("        docker build --progress=plain --platform linux/amd64 -t $CI_REGISTRY/formatter:$HASH -f Dockerfile.formatter .")
    lines.append("        docker push $CI_REGISTRY/formatter:$HASH")
    lines.append("        nydus-convert $CI_REGISTRY/formatter:$HASH $CI_REGISTRY/formatter:$HASH")
    lines.append("      fi")
    lines.append("  artifacts:")
    lines.append("    reports:")
    lines.append("      dotenv: formatter-image.env")
    lines.append("")

    # lint
    lines.append("lint:")
    lines.append("  extends: .build-and-test-fast")
    lines.append("  needs:")
    lines.append("    - build-formatter-image")
    lines.append("  image: $FORMATTER_IMAGE_TAG")
    lines.append('  tags: ["arch:amd64"]')
    lines.append("  script:")
    lines.append("    - make lint")
    lines.append("")

    # shellcheck
    lines.append("shellcheck:")
    lines.append("  extends: .build-and-test-fast")
    lines.append("  image: $CI_REGISTRY/nginx_musl_toolchain")
    lines.append('  tags: ["arch:amd64"]')
    lines.append("  script:")
    lines.append("    - find bin/ test/ example/ -type f -executable -not -name '*.py' | xargs shellcheck --exclude SC1071,SC1091,SC2317")
    lines.append("")

    # check-generated-ci
    lines.append("check-generated-ci:")
    lines.append("  extends: .build-and-test-fast")
    lines.append("  image: registry.ddbuild.io/ci/nginx-datadog/formatter:102793200")
    lines.append('  tags: ["arch:amd64"]')
    lines.append("  script:")
    lines.append("    - uv run bin/generate_gitlab_ci.py --check")
    lines.append("")

    # build-nginx-fast
    lines.append("build-nginx-fast:")
    lines.append("  extends:")
    lines.append("    - .build-and-test-fast")
    lines.append("    - .build-nginx")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        # The version used by system-tests must be one of the ones below. See https://github.com/DataDog/system-tests/pull/6113.")
    lines.append("        NGINX_VERSION:")
    lines.append(version_list_block(fast_nginx, indent=10))
    lines.append('        WAF: ["ON", "OFF"]')
    lines.append("")

    # build-ingress-nginx-fast
    lines.append("build-ingress-nginx-fast:")
    lines.append("  extends:")
    lines.append("    - .build-and-test-fast")
    lines.append("    - .build-ingress-nginx")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        INGRESS_NGINX_VERSION:")
    lines.append(version_list_block(fast_ingress, indent=10))
    lines.append("")

    # build-openresty-fast
    lines.append("build-openresty-fast:")
    lines.append("  extends:")
    lines.append("    - .build-and-test-fast")
    lines.append("    - .build-openresty")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        RESTY_VERSION:")
    lines.append(version_list_block(fast_openresty, indent=10))
    lines.append('        WAF: ["ON", "OFF"]')
    lines.append("")

    # build-nginx-rum-fast
    lines.append("build-nginx-rum-fast:")
    lines.append("  extends:")
    lines.append("    - .build-and-test-fast")
    lines.append("    - .build-nginx-rum")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        NGINX_VERSION:")
    lines.append(version_list_block(rum_versions, indent=10))
    lines.append('        WAF: ["OFF"]')
    lines.append("")

    # test-nginx-fast
    lines.append("test-nginx-fast:")
    lines.append("  extends:")
    lines.append("    - .build-and-test-fast")
    lines.append("    - .test-nginx")
    lines.append("  needs: [build-nginx-fast]")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append(nginx_test_matrix_entries(fast_nginx))
    lines.append(extra_test_matrix_entries(extras))
    lines.append("")

    # test-ingress-nginx-fast
    lines.append("test-ingress-nginx-fast:")
    lines.append("  extends:")
    lines.append("    - .build-and-test-fast")
    lines.append("    - .test-ingress-nginx")
    lines.append("  needs: [build-ingress-nginx-fast]")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        INGRESS_NGINX_VERSION:")
    lines.append(version_list_block(fast_ingress, indent=10))
    lines.append("")

    # test-openresty-fast
    lines.append("test-openresty-fast:")
    lines.append("  extends:")
    lines.append("    - .build-and-test-fast")
    lines.append("    - .test-openresty")
    lines.append("  needs: [build-openresty-fast]")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append('      - ARCH: ["amd64", "arm64"]')
    lines.append("        RESTY_VERSION:")
    lines.append(version_list_block(fast_openresty, indent=10))
    lines.append('        WAF: ["ON", "OFF"]')
    lines.append("")

    # test-nginx-rum-fast
    lines.append("test-nginx-rum-fast:")
    lines.append("  extends:")
    lines.append("    - .build-and-test-fast")
    lines.append("    - .test-nginx-rum")
    lines.append("  needs: [build-nginx-rum-fast]")
    lines.append("  parallel:")
    lines.append("    matrix:")
    lines.append(nginx_rum_test_matrix_entries(rum_versions))
    lines.append("")

    # coverage
    lines.append("coverage:")
    lines.append("  extends:")
    lines.append("    - .build-and-test-fast")
    lines.append("    - .build")
    lines.append("  variables:")
    lines.append('    ARCH: "amd64"')
    lines.append(f'    BASE_IMAGE: "nginx:{cov_version}"')
    lines.append(f'    NGINX_VERSION: "{cov_version}"')
    lines.append('    WAF: "ON"')
    lines.append('  tags: ["docker-in-docker:$ARCH"]')
    lines.append("  script:")
    lines.append("    - make coverage")
    lines.append("")

    return "\n".join(lines)


def check_or_write(generated, path, check_mode):
    """In check mode, compare and return True if different.
    In write mode, write the file."""
    if check_mode:
        try:
            with open(path) as f:
                existing = f.read()
        except FileNotFoundError:
            print(f"MISSING: {path}")
            return True

        if existing != generated:
            diff = difflib.unified_diff(
                existing.splitlines(keepends=True),
                generated.splitlines(keepends=True),
                fromfile=f"{path} (on disk)",
                tofile=f"{path} (generated)",
            )
            sys.stdout.writelines(diff)
            return True
        return False
    else:
        with open(path, "w") as f:
            f.write(generated)
        print(f"Wrote {path}")
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="Compare generated output with on-disk files; exit 1 on diff",
    )
    args = parser.parse_args()

    data = load_versions()
    all_content = generate_all(data)
    fast_content = generate_fast(data)

    any_diff = False
    any_diff |= check_or_write(all_content, ALL_FILE, args.check)
    any_diff |= check_or_write(fast_content, FAST_FILE, args.check)

    if args.check:
        if any_diff:
            print(
                "\nGenerated CI files are out of date. "
                "Run: python3 bin/generate_gitlab_ci.py"
            )
            sys.exit(1)
        else:
            print("Generated CI files are up to date.")


if __name__ == "__main__":
    main()
