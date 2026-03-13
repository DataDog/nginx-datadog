#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///
"""Manage mirrored Docker images: generate lock files, mirror, and lint.

Uses CLI tools (crane, skopeo, docker, podman, nerdctl) for registry
operations instead of Python registry libraries. Whichever tool is
available on PATH will be used.
"""

import argparse
import collections
import fnmatch
import functools
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

import yaml

# Force line-buffered output so parallel progress lines appear immediately.
print = functools.partial(print, flush=True)  # type: ignore[assignment]

PROJECT_DIR = os.getcwd()
SCRIPT_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
DEST_REGISTRY = "registry.ddbuild.io/ci/nginx-datadog/mirror"


def _find_mirror_yaml() -> str:
    cwd_yaml = os.path.join(PROJECT_DIR, "mirror_images.yaml")
    script_yaml = os.path.join(SCRIPT_DIR, "mirror_images.yaml")
    if os.path.exists(cwd_yaml):
        return cwd_yaml
    if os.path.exists(script_yaml):
        print(f"Note: using {script_yaml} (not found in cwd)", file=sys.stderr)
        return script_yaml
    return cwd_yaml  # will fail later with a clear FileNotFoundError


MIRROR_YAML = _find_mirror_yaml()
LOCK_YAML = os.path.join(os.path.dirname(MIRROR_YAML), "mirror_images.lock.yaml")

# ---------------------------------------------------------------------------
# Registry CLI abstraction
# ---------------------------------------------------------------------------


def _find_tool(names: list[str]) -> str | None:
    """Return the first tool name found on PATH."""
    for name in names:
        if shutil.which(name):
            return name
    return None


def _find_digest_tool() -> str:
    """Find a tool that can resolve image digests."""
    tool = _find_tool(["crane", "docker", "podman", "nerdctl"])
    if not tool:
        print(
            "No container tool found. Install one of: crane, docker, podman, nerdctl",
            file=sys.stderr)
        sys.exit(1)
    return tool


def _find_copy_tool() -> str:
    """Find a tool that can copy images between registries."""
    tool = _find_tool(["crane", "skopeo", "docker"])
    if not tool:
        print(
            "No image copy tool found. Install one of: crane, skopeo, docker",
            file=sys.stderr)
        sys.exit(1)
    return tool


def resolve_digest(image_ref: str, tool: str) -> str:
    """Resolve image:tag -> sha256:... using the given CLI tool."""
    if tool == "crane":
        cmd = ["crane", "digest", image_ref]
    elif tool == "skopeo":
        cmd = [
            "skopeo", "inspect", "--format", "{{.Digest}}",
            f"docker://{image_ref}"
        ]
    elif tool in ("docker", "podman", "nerdctl"):
        cmd = [tool, "manifest", "inspect", image_ref]
    else:
        raise ValueError(f"Unknown tool: {tool}")

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"{tool} failed for {image_ref}: {result.stderr.strip()}")

    if tool in ("crane", "skopeo"):
        digest = result.stdout.strip()
        if not digest.startswith("sha256:"):
            raise RuntimeError(
                f"{tool} returned unexpected digest format for {image_ref}: {digest!r}")
        return digest

    # docker/podman/nerdctl: compute digest from raw manifest output.
    # Note: this may differ from the registry's canonical digest if the
    # tool reformats the JSON. Prefer crane or skopeo for accurate digests.
    raw = result.stdout.strip().encode()
    return f"sha256:{hashlib.sha256(raw).hexdigest()}"


def check_digest_exists(image_ref_with_digest: str, tool: str) -> bool:
    """Check if a digest exists at a target registry."""
    if tool == "crane":
        cmd = ["crane", "manifest", image_ref_with_digest]
    elif tool == "skopeo":
        cmd = [
            "skopeo", "inspect", "--raw",
            f"docker://{image_ref_with_digest}",
        ]
    elif tool in ("docker", "podman", "nerdctl"):
        cmd = [tool, "manifest", "inspect", image_ref_with_digest]
    else:
        raise ValueError(f"Unknown tool: {tool}")

    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode == 0


def copy_image(source_ref: str, target: str, tool: str) -> tuple[bool, str]:
    """Copy an image from source to target. Returns (success, error_msg)."""
    if tool == "crane":
        cmd = [tool, "copy", "--platform", "all", source_ref, target]
    elif tool == "skopeo":
        cmd = [
            tool, "copy", "--all", f"docker://{source_ref}",
            f"docker://{target}"
        ]
    elif tool == "docker":
        cmd = [
            tool, "buildx", "imagetools", "create", "-t", target, source_ref
        ]
    else:
        raise ValueError(f"Unknown copy tool: {tool}")

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        return False, result.stderr.strip()
    return True, ""


def list_tags(image_repo: str, tool: str) -> list[str]:
    """List all tags for a repository."""
    if tool in ("docker", "podman", "nerdctl"):
        print(f"  [warning] {tool} does not support tag listing; "
              "version pinning will be skipped. Install crane or skopeo "
              "for full tag resolution.", file=sys.stderr)
        return []

    if tool == "crane":
        result = subprocess.run(["crane", "ls", image_repo],
                                capture_output=True,
                                text=True)
    elif tool == "skopeo":
        result = subprocess.run(
            ["skopeo", "list-tags", f"docker://{image_repo}"],
            capture_output=True,
            text=True,
        )
    else:
        raise ValueError(f"Unknown tool for tag listing: {tool}")

    if result.returncode != 0:
        print(
            f"  [warning] Failed to list tags for {image_repo}: "
            f"{result.stderr.strip()}",
            file=sys.stderr)
        return []

    if tool == "crane":
        return result.stdout.strip().splitlines()

    try:
        data = json.loads(result.stdout)
        return data.get("Tags", [])
    except json.JSONDecodeError as exc:
        print(f"  [warning] Failed to parse tags for {image_repo}: {exc}",
              file=sys.stderr)
        return []


# ---------------------------------------------------------------------------
# Image reference parsing & YAML config
# ---------------------------------------------------------------------------


def parse_image_ref(raw: str) -> str:
    """Normalize a Docker image reference to registry/repo:tag form."""
    ref = raw.strip()
    parts = ref.split("/", 1)
    if len(parts) == 1:
        registry = "index.docker.io"
        repo = f"library/{ref}"
    elif "." in parts[0] or ":" in parts[0]:
        registry = parts[0]
        repo = parts[1]
    else:
        registry = "index.docker.io"
        repo = ref

    if ":" not in repo:
        repo = f"{repo}:latest"

    return f"{registry}/{repo}"


def parse_mirror_yaml(path: str) -> dict[str, str]:
    """Parse mirror_images.yaml. Returns {source_ref: target} mapping."""
    with open(path) as f:
        entries = yaml.safe_load(f)

    if not isinstance(entries, list):
        raise ValueError(f"Expected a YAML list in {path}")

    images = {}
    for i, entry in enumerate(entries):
        if isinstance(entry, str):
            images[entry] = f"{DEST_REGISTRY}/{entry}"
        elif isinstance(entry, dict):
            for source, opts in entry.items():
                opts = opts or {}
                target = opts.get("target", f"{DEST_REGISTRY}/{source}")
                images[source] = target
        else:
            print(f"  [warning] Ignoring unexpected entry #{i} in {path}: "
                  f"{entry!r} (expected str or dict)", file=sys.stderr)
    return images


def parse_lock_yaml(path: str) -> dict[str, dict]:
    """Parse mirror_images.lock.yaml. Returns {source: {digest, target, tag}}."""
    with open(path) as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict) or "images" not in data:
        raise ValueError(
            f"Malformed lock file {path}: expected a YAML mapping with an 'images' key"
        )
    return data["images"]


# ---------------------------------------------------------------------------
# Tag resolution for vague tags (e.g. "latest")
# ---------------------------------------------------------------------------


def _version_sort_key(tag: str) -> list[tuple[int, int | str]]:
    """Sort key that orders version-like tags so the highest version comes last.

    Numeric parts sort before string parts so that "1.2.3" < "1.2.10".
    """
    parts = re.split(r"[.\-+]", tag.lstrip("v"))
    return [(0, int(p)) if p.isdigit() else (1, p) for p in parts]


def find_specific_tag(image_ref: str, digest: str, tool: str,
                      pool: ThreadPoolExecutor) -> str:
    """Given a vague tag (e.g. 'latest'), find the most specific version tag
    that shares the same digest."""
    # Extract repo without tag
    if ":" in image_ref:
        repo = image_ref.rsplit(":", 1)[0]
    else:
        repo = image_ref

    all_tags = list_tags(repo, tool)
    if not all_tags:
        return ""

    # "clean" = pure version tags like "1.2.3" or "v1.2.3"
    clean_re = re.compile(r"^v?\d+(\.\d+)+$")
    # "suffixed" = version + single suffix like "1.2.3-alpine"
    suffixed_re = re.compile(r"^v?\d+(\.\d+)+-[a-zA-Z][a-zA-Z0-9]*$")

    clean_tags = sorted(
        [t for t in all_tags if clean_re.match(t)],
        key=_version_sort_key, reverse=True,
    )
    clean_set = set(clean_tags)
    suffixed_tags = sorted(
        [t for t in all_tags if suffixed_re.match(t) and t not in clean_set],
        key=_version_sort_key, reverse=True,
    )

    candidates = clean_tags[:30] + suffixed_tags[:20]
    if not candidates:
        return ""

    def _resolve_tag(tag: str) -> tuple[str, str | None]:
        try:
            d = resolve_digest(f"{repo}:{tag}", tool)
            return tag, d
        except (RuntimeError, subprocess.SubprocessError, ValueError) as exc:
            print(f"    [debug] Could not resolve {repo}:{tag}: {exc}",
                  file=sys.stderr)
            return tag, None

    futures = {pool.submit(_resolve_tag, tag): tag for tag in candidates}
    matches = []
    for future in as_completed(futures):
        tag, tag_digest = future.result()
        if tag_digest == digest:
            matches.append(tag)

    if not matches:
        return ""

    # Prefer clean version tags (e.g. "1.2.3") over suffixed ones (e.g. "1.2.3-alpine")
    clean_matches = sorted(
        [t for t in matches if clean_re.match(t)],
        key=_version_sort_key, reverse=True,
    )
    if clean_matches:
        return clean_matches[0]

    matches.sort(key=_version_sort_key, reverse=True)
    return matches[0]


# ---------------------------------------------------------------------------
# Lock file
# ---------------------------------------------------------------------------


def _write_lock_file(path: str, results: dict) -> None:
    """Write the lock file with current results, sorted for stable output."""
    lock_data = {
        "version": 1,
        "images": {
            k: results[k]
            for k in sorted(results)
        },
    }
    dir_name = os.path.dirname(path) or "."
    fd, tmp = tempfile.mkstemp(dir=dir_name, suffix=".tmp", prefix=".lock_")
    try:
        with os.fdopen(fd, "w") as f:
            f.write("# Auto-generated by: uv run bin/mirror_images.py lock\n")
            f.write("# Do not edit manually.\n")
            yaml.dump(lock_data, f, default_flow_style=False, sort_keys=False)
        os.replace(tmp, path)
    except BaseException:
        os.unlink(tmp)
        raise


def cmd_lock(args: argparse.Namespace) -> int:
    """Generate mirror_images.lock.yaml with resolved digests."""
    tool = _find_digest_tool()
    images = parse_mirror_yaml(args.mirror_yaml)
    output_path = args.output or LOCK_YAML

    # Load existing lock file to preserve previously resolved entries
    results = {}
    if os.path.exists(output_path):
        try:
            existing = parse_lock_yaml(output_path)
            results.update(existing)
            print(f"Loaded {len(results)} existing entries from {output_path}")
        except (OSError, yaml.YAMLError, ValueError) as exc:
            print(
                f"WARNING: Could not parse existing lock file {output_path}: {exc}",
                file=sys.stderr)
            print("Resolving all images from scratch.", file=sys.stderr)

    def _current_lock_entries() -> dict:
        """Return only the results that are still declared in mirror_images.yaml."""
        return {k: results[k] for k in results if k in images}

    # Only resolve images not already in the lock file
    to_resolve = {
        src: tgt
        for src, tgt in images.items() if src not in results
    }
    if not to_resolve:
        print(f"All {len(images)} images already resolved in {output_path}")
        _write_lock_file(output_path, _current_lock_entries())
        return 0

    print(
        f"Resolving {len(to_resolve)} of {len(images)} images using {tool} (parallelism={args.jobs})..."
    )

    errors = []
    progress_lock = threading.Lock()

    def _resolve(source: str, target: str, pool: ThreadPoolExecutor):
        digest = resolve_digest(source, tool)
        normalized = parse_image_ref(source)
        tag = normalized.rsplit(":", 1)[1]

        if tag == "latest":
            resolved_tag = find_specific_tag(source, digest, tool, pool) or tag
        else:
            resolved_tag = tag

        return source, target, digest, resolved_tag

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(_resolve, src, tgt, pool): src
            for src, tgt in to_resolve.items()
        }
        for future in as_completed(futures):
            src = futures[future]
            try:
                source, target, digest, tag = future.result()
                original_tag = source.rsplit(":", 1)[-1]
                tag_info = f" (tag: {tag})" if tag != original_tag else ""
                print(f"  {source}: {digest}{tag_info}")
                with progress_lock:
                    results[source] = {
                        "digest": digest,
                        "target": target,
                        "tag": tag,
                    }
                    _write_lock_file(output_path, _current_lock_entries())
            except (RuntimeError, subprocess.SubprocessError, ValueError) as exc:
                errors.append((src, str(exc)))
                print(f"  ERROR {src}: {exc}", file=sys.stderr)

    if errors:
        print(f"\nFailed to resolve {len(errors)} image(s):", file=sys.stderr)
        for src, err in errors:
            print(f"  - {src}: {err}", file=sys.stderr)
        return 1

    print(f"\nWrote {output_path} ({len(results)} images)")
    return 0


# ---------------------------------------------------------------------------
# Mirror
# ---------------------------------------------------------------------------


def cmd_mirror(args: argparse.Namespace) -> int:
    """Sync images from lock file to target registry using crane/skopeo."""
    lock_path = args.lock_yaml
    if not os.path.exists(lock_path):
        print(f"Lock file not found: {lock_path}", file=sys.stderr)
        print("Run 'mirror_images.py lock' first.", file=sys.stderr)
        return 1

    digest_tool = _find_digest_tool()
    copy_tool = _find_copy_tool()
    images = parse_lock_yaml(lock_path)

    # Phase 1: check which images need copying
    print(
        f"Phase 1: checking {len(images)} images using {digest_tool} ({args.jobs} workers)..."
    )

    def _check_if_mirrored(source: str, info: dict):
        target = info["target"]
        digest = info["digest"]
        target_repo = target.rsplit(":", 1)[0] if ":" in target else target
        digest_ref = f"{target_repo}@{digest}"
        print(f"  ? {source} -> {target}")
        present = check_digest_exists(digest_ref, digest_tool)
        return source, info, present

    to_copy = []
    already_present = 0
    checked = 0

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(_check_if_mirrored, src, info): src
            for src, info in images.items()
        }
        for future in as_completed(futures):
            src = futures[future]
            checked += 1
            try:
                source, info, present = future.result()
            except (RuntimeError, subprocess.SubprocessError, ValueError) as exc:
                print(f"  ERROR checking {src}: {exc} — will attempt copy",
                      file=sys.stderr)
                to_copy.append((src, images[src]))
                continue
            if present:
                already_present += 1
                print(
                    f"  = [{checked}/{len(images)}] {source} (already mirrored)"
                )
            else:
                to_copy.append((source, info))
                print(f"  + [{checked}/{len(images)}] {source} (needs copy)")

    if not to_copy:
        print(f"\nAll {len(images)} images are up to date.")
        return 0

    print(
        f"\n{already_present} already mirrored, {len(to_copy)} to copy using {copy_tool}.\n"
    )

    # Phase 2: copy missing images
    print(f"\nPhase 2: copying {len(to_copy)} images using {copy_tool}...")
    errors = []
    for i, (source, info) in enumerate(sorted(to_copy), 1):
        digest = info["digest"]
        target = info["target"]
        source_ref = f"{parse_image_ref(source).rsplit(':', 1)[0]}@{digest}"
        print(f"  -> [{i}/{len(to_copy)}] {source} ({digest[:19]}...)")
        print(f"     src:  {source_ref}")
        print(f"     dest: {target}")
        if args.dry_run:
            print(f"     [dry-run] {copy_tool} copy {source_ref} {target}")
            continue
        ok, err = copy_image(source_ref, target, copy_tool)
        if not ok:
            errors.append((source, err))
            print(f"     ERROR: {err}", file=sys.stderr)
        else:
            print(f"     OK")

    if errors:
        print(f"\nFailed to copy {len(errors)} image(s):", file=sys.stderr)
        for src, err in errors:
            print(f"  - {src}: {err}", file=sys.stderr)
        return 1

    action = "would copy" if args.dry_run else "copied"
    print(
        f"\nDone: {already_present} already present, {len(to_copy)} {action}.")
    return 0


# ---------------------------------------------------------------------------
# Lint
# ---------------------------------------------------------------------------


def find_public_image_refs() -> list[tuple[str, int, str, str]]:
    """Scan the repo for image references not using registry.ddbuild.io."""
    internal_prefixes = ("registry.ddbuild.io/", "486234852809.dkr.ecr")
    hits = []

    _ignore_prefixes = internal_prefixes + (
        "$", "{", "nginx-datadog-test-",
    )

    def _is_external(img: str) -> bool:
        if any(img.startswith(p) for p in _ignore_prefixes):
            return False
        return bool(re.match(r"^[a-zA-Z0-9]", img))

    def _read_lines(filepath):
        try:
            with open(filepath) as f:
                return list(enumerate(f, 1))
        except OSError as exc:
            print(f"  [warning] Could not read {filepath}: {exc}",
                  file=sys.stderr)
            return []

    # Skip git submodules (external repos) and example/ (uses public images intentionally).
    _skip_dirs = tuple(
        os.path.join(PROJECT_DIR, d) for d in ("dd-trace-cpp", "libddwaf", "example"))

    def _in_skip_dir(filepath):
        return filepath.startswith(_skip_dirs)

    def _scan_dockerfiles():
        for filepath in glob.glob(os.path.join(PROJECT_DIR, "**/Dockerfile*"),
                                  recursive=True):
            if "/.git/" in filepath or _in_skip_dir(filepath):
                continue
            for lineno, line in _read_lines(filepath):
                stripped = line.strip()
                m = re.match(r"^FROM\s+(\S+)", stripped, re.IGNORECASE)
                if m and _is_external(m.group(1)):
                    hits.append((filepath, lineno, stripped, m.group(1)))
                for m in re.finditer(r"--from=(\S+)", stripped):
                    img = m.group(1)
                    if ("/" in img or ":" in img) and _is_external(img):
                        hits.append((filepath, lineno, stripped, img))

    def _scan_yaml_image_fields():
        skip_path_fragments = ("/.git/", "/.gitlab/")
        for pattern in ("**/*.yml", "**/*.yaml"):
            for filepath in glob.glob(os.path.join(PROJECT_DIR, pattern),
                                      recursive=True):
                if any(frag in filepath for frag in skip_path_fragments):
                    continue
                if _in_skip_dir(filepath):
                    continue
                for lineno, line in _read_lines(filepath):
                    m = re.match(r"^\s+image:\s+['\"]?(\S+?)['\"]?\s*$", line)
                    if m and _is_external(m.group(1)):
                        hits.append(
                            (filepath, lineno, line.rstrip(), m.group(1)))

    _scan_dockerfiles()
    _scan_yaml_image_fields()
    return hits


def _collect_gitlab_ci_files(entry: str) -> list[str]:
    """Starting from a GitLab CI YAML file, follow local includes and return all file paths."""
    collected = []
    visited: set[str] = set()
    queue: collections.deque[str] = collections.deque([entry])
    while queue:
        path = queue.popleft()
        abspath = os.path.join(PROJECT_DIR, path) if not os.path.isabs(path) else path
        if abspath in visited or not os.path.isfile(abspath):
            continue
        visited.add(abspath)
        collected.append(abspath)
        try:
            with open(abspath) as f:
                data = yaml.safe_load(f)
        except (OSError, yaml.YAMLError) as exc:
            print(f"  [warning] Could not parse {abspath}: {exc}",
                  file=sys.stderr)
            continue
        if not isinstance(data, dict):
            continue
        includes = data.get("include", [])
        if isinstance(includes, dict):
            includes = [includes]
        if not isinstance(includes, list):
            continue
        for inc in includes:
            if isinstance(inc, str):
                queue.append(os.path.join(PROJECT_DIR, inc))
            elif isinstance(inc, dict) and "local" in inc:
                queue.append(os.path.join(PROJECT_DIR, inc["local"]))
    return collected


# Templates that map matrix variable names to image name patterns.
# {value} is replaced with the matrix variable value.
_GITLAB_MATRIX_IMAGE_TEMPLATES: dict[str, list[str]] = {
    "BASE_IMAGE": ["{value}"],
    "INGRESS_NGINX_VERSION": [
        "registry.k8s.io/ingress-nginx/controller:v{value}"
    ],
    "RESTY_VERSION": ["openresty/openresty:{value}-alpine"],
}


def _extract_matrix_combos(job: dict) -> list[dict]:
    """Extract the parallel:matrix combo list from a GitLab CI job definition."""
    parallel = job.get("parallel")
    if not isinstance(parallel, dict):
        return []
    matrix = parallel.get("matrix")
    if not isinstance(matrix, list):
        return []
    return [c for c in matrix if isinstance(c, dict)]


def _expand_matrix_images(combo: dict) -> list[str]:
    """Expand a single matrix combo into image references using known templates."""
    images = []
    for var_name, templates in _GITLAB_MATRIX_IMAGE_TEMPLATES.items():
        values = combo.get(var_name, [])
        if isinstance(values, str):
            values = [values]
        if not isinstance(values, list):
            continue
        for val in values:
            for tmpl in templates:
                images.append(tmpl.format(value=val))
    return images


def find_gitlab_ci_images() -> list[tuple[str, str]]:
    """Extract public image references from GitLab CI matrix variables.

    Returns a list of (source_file_relpath, image_ref) tuples.
    """
    ci_entry = os.path.join(PROJECT_DIR, ".gitlab-ci.yml")
    if not os.path.isfile(ci_entry):
        return []

    results = []
    for filepath in _collect_gitlab_ci_files(ci_entry):
        try:
            with open(filepath) as f:
                data = yaml.safe_load(f)
        except (OSError, yaml.YAMLError) as exc:
            print(f"  [warning] Could not parse {filepath}: {exc}",
                  file=sys.stderr)
            continue
        if not isinstance(data, dict):
            continue

        relpath = os.path.relpath(filepath, PROJECT_DIR)
        for _job_name, job in data.items():
            if not isinstance(job, dict):
                continue
            for combo in _extract_matrix_combos(job):
                for img in _expand_matrix_images(combo):
                    results.append((relpath, img))

    return results


def cmd_lint(args: argparse.Namespace) -> int:
    """Check that all images are referenced from registry.ddbuild.io."""
    declared = parse_mirror_yaml(args.mirror_yaml)
    rc = 0

    # --- Check 1: public image references in Dockerfiles and YAML ---
    public_refs = find_public_image_refs()

    if public_refs:
        by_image: dict[str, list[tuple[str, int, str]]] = {}
        for filepath, lineno, line, img in public_refs:
            by_image.setdefault(img, []).append((filepath, lineno, line))

        undeclared = []
        print(
            "Public image references found (should use registry.ddbuild.io mirror):\n"
        )
        for img in sorted(by_image):
            replacement = declared.get(img)
            if not replacement:
                replacement = f"{DEST_REGISTRY}/{img}"
                undeclared.append(img)

            print(f"  {img}")
            print(f"    -> {replacement}")
            for filepath, lineno, line in by_image[img]:
                relpath = os.path.relpath(filepath, PROJECT_DIR)
                print(f"       {relpath}:{lineno}")
            print()

        if undeclared:
            print(
                "Images not declared in mirror_images.yaml (add them first):")
            for img in sorted(undeclared):
                print(f"  - {img}")
            print()

        print(
            f"{len(public_refs)} public image reference(s) across {len(by_image)} image(s)."
        )
        rc = 1

    # --- Check 2: GitLab CI matrix images must be in mirror_images.yaml ---
    ci_images = find_gitlab_ci_images()
    if ci_images:
        missing: dict[str, list[str]] = {}
        for source_file, img in ci_images:
            if img not in declared:
                missing.setdefault(img, []).append(source_file)

        if missing:
            print("GitLab CI matrix images not declared in mirror_images.yaml:\n")
            for img in sorted(missing):
                sources = sorted(set(missing[img]))
                print(f"  - {img}")
                for src in sources:
                    print(f"      {src}")
            print(
                f"\n{len(missing)} image(s) from GitLab CI not in mirror_images.yaml."
            )
            rc = 1
        else:
            print("All GitLab CI matrix images are declared in mirror_images.yaml.")

    if rc == 0:
        print("All image references use registry.ddbuild.io.")
    return rc


# ---------------------------------------------------------------------------
# Add images
# ---------------------------------------------------------------------------


def cmd_add(args: argparse.Namespace) -> int:
    """Add one or more images to mirror_images.yaml (if not already present)."""
    with open(args.mirror_yaml) as f:
        entries = yaml.safe_load(f)

    if not isinstance(entries, list):
        print(f"ERROR: {args.mirror_yaml} is empty or not a YAML list.",
              file=sys.stderr)
        return 1

    existing = set()
    for entry in entries:
        if isinstance(entry, str):
            existing.add(entry)
        elif isinstance(entry, dict):
            existing.update(entry.keys())

    added = []
    for img in args.images:
        if img in existing:
            print(f"  already listed: {img}")
        else:
            entries.append(img)
            added.append(img)
            print(f"  added: {img}")

    if not added:
        print("\nNo new images to add.")
        return 0

    # Sort simple string entries; keep dicts in place
    strings = sorted([e for e in entries if isinstance(e, str)])
    dicts = [e for e in entries if isinstance(e, dict)]
    entries = strings + dicts

    # Rewrite the YAML file preserving the header comments and quoting style
    with open(args.mirror_yaml) as f:
        header_lines = []
        for line in f:
            if line.startswith("#") or line.strip() == "":
                header_lines.append(line)
            else:
                break

    with open(args.mirror_yaml, "w") as f:
        f.writelines(header_lines)
        for entry in entries:
            if isinstance(entry, str):
                f.write(f'- "{entry}"\n')
            elif isinstance(entry, dict):
                yaml.dump([entry],
                          f,
                          default_flow_style=False,
                          sort_keys=False)

    print(f"\nAdded {len(added)} image(s) to {args.mirror_yaml}")
    return 0


# ---------------------------------------------------------------------------
# Relock images
# ---------------------------------------------------------------------------


def cmd_relock(args: argparse.Namespace) -> int:
    """Re-resolve digests for images matching the given patterns.

    Removes matching entries from the lock file, then runs the lock
    command to re-resolve them. Patterns use fnmatch-style wildcards
    (e.g. 'nginx:1.29.*', 'openresty/*').
    """
    lock_path = args.output or LOCK_YAML
    if not os.path.exists(lock_path):
        print(f"Lock file not found: {lock_path}", file=sys.stderr)
        print("Run 'lock' first to create it.", file=sys.stderr)
        return 1

    existing = parse_lock_yaml(lock_path)
    patterns = args.patterns

    matched = []
    for src in list(existing):
        if any(fnmatch.fnmatch(src, pat) for pat in patterns):
            matched.append(src)
            del existing[src]

    if not matched:
        print(f"No images in lock file matched: {', '.join(patterns)}")
        return 1

    print(f"Removing {len(matched)} image(s) from lock file to re-resolve:")
    for src in sorted(matched):
        print(f"  - {src}")

    # Write out the lock file without the matched entries
    _write_lock_file(lock_path, existing)

    # Now run the normal lock flow which will resolve the missing entries
    print()
    return cmd_lock(args)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Manage mirrored Docker images")
    parser.add_argument(
        "--mirror-yaml",
        default=MIRROR_YAML,
        help=f"Path to mirror_images.yaml (default: {MIRROR_YAML})",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    lock_parser = subparsers.add_parser(
        "lock", help="Generate mirror_images.lock.yaml with resolved digests")
    lock_parser.set_defaults(func=cmd_lock)
    lock_parser.add_argument("-j",
                             "--jobs",
                             type=int,
                             default=16,
                             help="Parallel workers (default: 16)")
    lock_parser.add_argument(
        "-o",
        "--output",
        help="Output path (default: mirror_images.lock.yaml)")

    mirror_parser = subparsers.add_parser(
        "mirror", help="Sync images from lock file to target registry")
    mirror_parser.set_defaults(func=cmd_mirror)
    mirror_parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=16,
        help="Parallel workers for checking (default: 16)")
    mirror_parser.add_argument("--lock-yaml",
                               default=LOCK_YAML,
                               help="Path to lock file")
    mirror_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be copied without copying")

    lint_parser = subparsers.add_parser(
        "lint", help="Check that all image references use registry.ddbuild.io")
    lint_parser.set_defaults(func=cmd_lint)

    add_parser = subparsers.add_parser(
        "add",
        help="Add images to mirror_images.yaml",
        description=
        "Add one or more Docker image references to mirror_images.yaml. "
        "Images already listed are skipped. The file is re-sorted after adding."
    )
    add_parser.set_defaults(func=cmd_add)
    add_parser.add_argument(
        "images",
        nargs="+",
        metavar="IMAGE",
        help="Image references to add (e.g. 'nginx:1.30.0' 'alpine:3.21')")

    relock_parser = subparsers.add_parser(
        "relock",
        help="Re-resolve digests for specific images in the lock file",
        description="Remove matching entries from mirror_images.lock.yaml and "
        "re-resolve their digests. Uses fnmatch-style wildcards. "
        "Example: 'nginx:1.29.*' matches nginx:1.29.0, nginx:1.29.5-alpine, etc."
    )
    relock_parser.set_defaults(func=cmd_relock)
    relock_parser.add_argument(
        "patterns",
        nargs="+",
        metavar="PATTERN",
        help=
        "Glob patterns to match image names (e.g. 'nginx:1.29.*' 'openresty/*')"
    )
    relock_parser.add_argument("-j",
                               "--jobs",
                               type=int,
                               default=16,
                               help="Parallel workers (default: 16)")
    relock_parser.add_argument(
        "-o",
        "--output",
        help="Path to lock file (default: mirror_images.lock.yaml)")

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
