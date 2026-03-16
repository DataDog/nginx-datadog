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
    sys.exit("PyYAML is required.  Install it with:  pip install pyyaml")

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSIONS_FILE = os.path.join(REPO_ROOT, "supported_versions.yml")
ALL_FILE = os.path.join(REPO_ROOT, ".gitlab", "build-and-test-all.yml")
FAST_FILE = os.path.join(REPO_ROOT, ".gitlab", "build-and-test-fast.yml")

# Each nginx test matrix entry = 8 jobs: 2 arch × 2 base_image × 2 WAF.
# Extra test images = 4 jobs each: 2 arch × 1 base_image × 2 WAF.
JOBS_PER_NGINX_VERSION = 8
JOBS_PER_EXTRA_IMAGE = 4
GITLAB_MATRIX_LIMIT = 200


# ---------------------------------------------------------------------------
# Version extraction (sorted for stable output)
# ---------------------------------------------------------------------------

def _sort_key(version_str):
    return tuple(int(p) for p in version_str.split("."))


def _versions(entries, flag=None):
    if flag:
        entries = [e for e in entries if e.get(flag)]
    return sorted((e["version"] for e in entries), key=_sort_key)


def _extras(data):
    return sorted(
        data["nginx"].get("extra_test_images", []),
        key=lambda e: _sort_key(e["nginx_version"]),
    )


def load_versions():
    with open(VERSIONS_FILE) as f:
        return yaml.safe_load(f)


# ---------------------------------------------------------------------------
# YAML fragments — each returns a string starting at column 0
# ---------------------------------------------------------------------------

def _version_items(versions):
    """Indented version list items for inside a matrix entry (10-space indent)."""
    return "\n".join(f'          - "{v}"' for v in versions)


def _waf_line(waf):
    if waf is True:
        return '        WAF: ["ON", "OFF"]\n'
    elif waf:
        return f'        WAF: ["{waf}"]\n'
    return ""


def build_matrix_job(name, extends, var_name, versions, waf=True, comment=None):
    """A build job with ARCH × version [× WAF] matrix."""
    comment_line = f"        {comment}\n" if comment else ""
    return (
        f"{name}:\n"
        f"  extends:\n"
        f"    - {extends[0]}\n"
        f"    - {extends[1]}\n"
        f"  parallel:\n"
        f"    matrix:\n"
        f'      - ARCH: ["amd64", "arm64"]\n'
        f"{comment_line}"
        f"        {var_name}:\n"
        f"{_version_items(versions)}\n"
        f"{_waf_line(waf)}\n"
    )


def test_matrix_job(name, extends, var_name, versions, waf=True, needs=None):
    """A test job with ARCH × version [× WAF] matrix."""
    needs_line = f"  needs: [{needs}]\n" if needs else ""
    return (
        f"{name}:\n"
        f"  extends:\n"
        f"    - {extends[0]}\n"
        f"    - {extends[1]}\n"
        f"{needs_line}"
        f"  parallel:\n"
        f"    matrix:\n"
        f'      - ARCH: ["amd64", "arm64"]\n'
        f"        {var_name}:\n"
        f"{_version_items(versions)}\n"
        f"{_waf_line(waf)}\n"
    )


def _nginx_test_entries(versions):
    """Per-version test entries: ARCH × BASE_IMAGE(+alpine) × WAF."""
    entries = []
    for v in versions:
        entries.append(
            f'      - ARCH: ["amd64", "arm64"]\n'
            f'        BASE_IMAGE: ["nginx:{v}", "nginx:{v}-alpine"]\n'
            f'        NGINX_VERSION: ["{v}"]\n'
            f'        WAF: ["ON", "OFF"]'
        )
    return "\n".join(entries)


def _extra_test_entries(images):
    """Extra base image test entries (e.g. amazonlinux)."""
    entries = []
    for img in images:
        entries.append(
            f'      - ARCH: ["amd64", "arm64"]\n'
            f'        BASE_IMAGE: ["{img["base_image"]}"]\n'
            f'        NGINX_VERSION: ["{img["nginx_version"]}"]\n'
            f'        WAF: ["ON", "OFF"]'
        )
    return "\n".join(entries)


def _rum_test_entries(versions):
    """RUM test entries: WAF OFF only, no alpine."""
    entries = []
    for v in versions:
        entries.append(
            f'      - ARCH: ["amd64", "arm64"]\n'
            f'        BASE_IMAGE: ["nginx:{v}"]\n'
            f'        NGINX_VERSION: ["{v}"]\n'
            f'        WAF: ["OFF"]'
        )
    return "\n".join(entries)


def nginx_test_job(name, extends, versions, extras=None, needs=None):
    """Test-nginx job with per-version BASE_IMAGE entries + optional extras."""
    needs_line = f"  needs: [{needs}]\n" if needs else ""
    matrix = _nginx_test_entries(versions)
    if extras:
        matrix += "\n" + _extra_test_entries(extras)
    return (
        f"{name}:\n"
        f"  extends:\n"
        f"    - {extends[0]}\n"
        f"    - {extends[1]}\n"
        f"{needs_line}"
        f"  parallel:\n"
        f"    matrix:\n"
        f"{matrix}\n"
        f"\n"
    )


def rum_test_job(name, extends, versions, needs=None):
    """Test-nginx-rum job: WAF OFF only, no alpine."""
    needs_line = f"  needs: [{needs}]\n" if needs else ""
    return (
        f"{name}:\n"
        f"  extends:\n"
        f"    - {extends[0]}\n"
        f"    - {extends[1]}\n"
        f"{needs_line}"
        f"  parallel:\n"
        f"    matrix:\n"
        f"{_rum_test_entries(versions)}\n"
        f"\n"
    )


# ---------------------------------------------------------------------------
# Split logic for the 200-job GitLab matrix limit
# ---------------------------------------------------------------------------

def split_nginx_test_versions(all_versions, extras):
    """Split versions across test-nginx-all / test-nginx-all-bis.

    Returns (first_versions, first_extras, second_versions, second_extras).
    """
    job_count = 0
    split_at = len(all_versions)
    for i, _ in enumerate(all_versions):
        job_count += JOBS_PER_NGINX_VERSION
        if job_count > GITLAB_MATRIX_LIMIT:
            split_at = i
            break

    first = all_versions[:split_at]
    second = all_versions[split_at:]

    extra_jobs = len(extras) * JOBS_PER_EXTRA_IMAGE
    if len(first) * JOBS_PER_NGINX_VERSION + extra_jobs <= GITLAB_MATRIX_LIMIT:
        return first, extras, second, []
    return first, [], second, extras


# ---------------------------------------------------------------------------
# File generators
# ---------------------------------------------------------------------------

ALL_HEADER = """\
include:
  - local: ".gitlab/common.yml"

# We split build and test all in two stages because GitLab is not able to handle dependencies graph
# with more than 100 jobs!

.build-and-test-all:
  rules:
    - if: $CI_COMMIT_TAG =~ /^v[0-9]+\\.[0-9]+\\.[0-9]+/
      when: always
    - when: never

.build-all:
  extends: .build-and-test-all
  stage: build-all

.test-all:
  extends: .build-and-test-all
  stage: test-all

"""

FAST_HEADER = """\
include:
  - local: ".gitlab/common.yml"

.build-and-test-fast:
  stage: build-and-test-fast
  rules:
    - if: $CI_COMMIT_TAG
      when: never
    - when: always

build-formatter-image:
  extends: .build-and-test-fast
  image: registry.ddbuild.io/images/nydus:v2.3.8-dd
  tags: ["docker-in-docker:amd64"]
  script:
    - |
      HASH=$(cat Dockerfile.formatter | sha256sum | cut -d ' ' -f 1)
    - echo "FORMATTER_IMAGE_TAG=$CI_REGISTRY/formatter:$HASH" >> formatter-image.env
    - |
      if docker buildx imagetools inspect "$CI_REGISTRY/formatter:$HASH" > /dev/null 2>&1; then
        echo "Image $CI_REGISTRY/formatter:$HASH already exists. Skipping build."
      else
        docker build --progress=plain --platform linux/amd64 -t $CI_REGISTRY/formatter:$HASH -f Dockerfile.formatter .
        docker push $CI_REGISTRY/formatter:$HASH
        nydus-convert $CI_REGISTRY/formatter:$HASH $CI_REGISTRY/formatter:$HASH
      fi
  artifacts:
    reports:
      dotenv: formatter-image.env

lint:
  extends: .build-and-test-fast
  needs:
    - build-formatter-image
  image: $FORMATTER_IMAGE_TAG
  tags: ["arch:amd64"]
  script:
    - make lint

shellcheck:
  extends: .build-and-test-fast
  image: $CI_REGISTRY/nginx_musl_toolchain
  tags: ["arch:amd64"]
  script:
    - find bin/ test/ example/ -type f -executable -not -name '*.py' | xargs shellcheck --exclude SC1071,SC1091,SC2317

check-generated-ci:
  extends: .build-and-test-fast
  image: registry.ddbuild.io/ci/nginx-datadog/formatter:102793200
  tags: ["arch:amd64"]
  script:
    - uv run bin/generate_gitlab_ci.py --check

"""

SYSTEM_TESTS_COMMENT = (
    "# The version used by system-tests must be one of the ones below."
    " See https://github.com/DataDog/system-tests/pull/6113."
)


def generate_all(data):
    nginx = _versions(data["nginx"]["versions"])
    rum = _versions(data["nginx"]["versions"], "rum")
    ingress = _versions(data["ingress_nginx"]["versions"])
    openresty = _versions(data["openresty"]["versions"])
    extras = _extras(data)

    first_v, first_ex, second_v, second_ex = split_nginx_test_versions(nginx, extras)

    return (
        ALL_HEADER
        + build_matrix_job("build-nginx-all",
                           [".build-all", ".build-nginx"],
                           "NGINX_VERSION", nginx)
        + build_matrix_job("build-nginx-rum-all",
                           [".build-all", ".build-nginx-rum"],
                           "NGINX_VERSION", rum, waf="OFF")
        + build_matrix_job("build-ingress-nginx-all",
                           [".build-all", ".build-ingress-nginx"],
                           "INGRESS_NGINX_VERSION", ingress, waf=False)
        + build_matrix_job("build-openresty-all",
                           [".build-all", ".build-openresty"],
                           "RESTY_VERSION", openresty)
        + nginx_test_job("test-nginx-all",
                         [".test-all", ".test-nginx"],
                         first_v, first_ex)
        + "# GitLab is not able to handle a matrix with more than 200 jobs!\n"
        + nginx_test_job("test-nginx-all-bis",
                         [".test-all", ".test-nginx"],
                         second_v, second_ex)
        + rum_test_job("test-nginx-rum-all",
                       [".test-all", ".test-nginx-rum"], rum)
        + test_matrix_job("test-ingress-nginx-all",
                          [".test-all", ".test-ingress-nginx"],
                          "INGRESS_NGINX_VERSION", ingress, waf=False)
        + test_matrix_job("test-openresty-all",
                          [".test-all", ".test-openresty"],
                          "RESTY_VERSION", openresty)
    ).rstrip("\n") + "\n"


def generate_fast(data):
    fast_nginx = _versions(data["nginx"]["versions"], "fast")
    rum = _versions(data["nginx"]["versions"], "rum")
    fast_ingress = _versions(data["ingress_nginx"]["versions"], "fast")
    fast_openresty = _versions(data["openresty"]["versions"], "fast")
    extras = _extras(data)
    cov = data["nginx"]["coverage_version"]

    return (
        FAST_HEADER
        + build_matrix_job("build-nginx-fast",
                           [".build-and-test-fast", ".build-nginx"],
                           "NGINX_VERSION", fast_nginx,
                           comment=SYSTEM_TESTS_COMMENT)
        + build_matrix_job("build-ingress-nginx-fast",
                           [".build-and-test-fast", ".build-ingress-nginx"],
                           "INGRESS_NGINX_VERSION", fast_ingress, waf=False)
        + build_matrix_job("build-openresty-fast",
                           [".build-and-test-fast", ".build-openresty"],
                           "RESTY_VERSION", fast_openresty)
        + build_matrix_job("build-nginx-rum-fast",
                           [".build-and-test-fast", ".build-nginx-rum"],
                           "NGINX_VERSION", rum, waf="OFF")
        + nginx_test_job("test-nginx-fast",
                         [".build-and-test-fast", ".test-nginx"],
                         fast_nginx, extras, needs="build-nginx-fast")
        + test_matrix_job("test-ingress-nginx-fast",
                          [".build-and-test-fast", ".test-ingress-nginx"],
                          "INGRESS_NGINX_VERSION", fast_ingress, waf=False,
                          needs="build-ingress-nginx-fast")
        + test_matrix_job("test-openresty-fast",
                          [".build-and-test-fast", ".test-openresty"],
                          "RESTY_VERSION", fast_openresty,
                          needs="build-openresty-fast")
        + rum_test_job("test-nginx-rum-fast",
                       [".build-and-test-fast", ".test-nginx-rum"],
                       rum, needs="build-nginx-rum-fast")
        + f'coverage:\n'
          f'  extends:\n'
          f'    - .build-and-test-fast\n'
          f'    - .build\n'
          f'  variables:\n'
          f'    ARCH: "amd64"\n'
          f'    BASE_IMAGE: "nginx:{cov}"\n'
          f'    NGINX_VERSION: "{cov}"\n'
          f'    WAF: "ON"\n'
          f'  tags: ["docker-in-docker:$ARCH"]\n'
          f'  script:\n'
          f'    - make coverage\n'
    )


# ---------------------------------------------------------------------------
# Check / write
# ---------------------------------------------------------------------------

def check_or_write(generated, path, check_mode):
    if check_mode:
        try:
            with open(path) as f:
                existing = f.read()
        except FileNotFoundError:
            print(f"MISSING: {path}")
            return True
        if existing != generated:
            sys.stdout.writelines(difflib.unified_diff(
                existing.splitlines(keepends=True),
                generated.splitlines(keepends=True),
                fromfile=f"{path} (on disk)",
                tofile=f"{path} (generated)",
            ))
            return True
        return False
    else:
        with open(path, "w") as f:
            f.write(generated)
        print(f"Wrote {path}")
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="Exit 1 if generated files differ from on-disk")
    args = parser.parse_args()

    data = load_versions()
    any_diff = False
    any_diff |= check_or_write(generate_all(data), ALL_FILE, args.check)
    any_diff |= check_or_write(generate_fast(data), FAST_FILE, args.check)

    if args.check:
        if any_diff:
            print("\nGenerated CI files are out of date. "
                  "Run: uv run bin/generate_gitlab_ci.py")
            sys.exit(1)
        else:
            print("Generated CI files are up to date.")


if __name__ == "__main__":
    main()
