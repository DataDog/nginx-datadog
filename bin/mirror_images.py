#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyyaml"]
# ///
"""Manage mirrored Docker images: generate lock files, mirror, and lint.

Uses CLI tools (crane, skopeo, docker, podman, nerdctl) for registry
operations instead of Python registry libraries. Whichever tool is
available on PATH will be used.
"""

import functools

print = functools.partial(print, flush=True)

import argparse
import fnmatch
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

import yaml

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
DEST_REGISTRY = "registry.ddbuild.io/ci/nginx-datadog/mirror"
MIRROR_YAML = os.path.join(PROJECT_DIR, "mirror_images.yaml")
LOCK_YAML = os.path.join(PROJECT_DIR, "mirror_images.lock.yaml")

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
        return result.stdout.strip()

    # docker/podman/nerdctl: compute digest from raw manifest output.
    # Note: this may differ from the registry's canonical digest if the
    # tool reformats the JSON. Prefer crane or skopeo for accurate digests.
    raw = result.stdout.strip().encode()
    return f"sha256:{hashlib.sha256(raw).hexdigest()}"


def check_digest_exists(image_ref_with_digest: str, tool: str) -> bool:
    """Check if a digest exists at a target registry."""
    if tool == "crane":
        result = subprocess.run(
            ["crane", "manifest", image_ref_with_digest],
            capture_output=True,
            text=True,
        )
    elif tool in ("docker", "podman", "nerdctl"):
        result = subprocess.run(
            [tool, "manifest", "inspect", image_ref_with_digest],
            capture_output=True,
            text=True,
        )
    elif tool == "skopeo":
        result = subprocess.run(
            [
                "skopeo", "inspect", "--raw",
                f"docker://{image_ref_with_digest}"
            ],
            capture_output=True,
            text=True,
        )
    else:
        raise ValueError(f"Unknown tool: {tool}")
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
        return []

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
# YAML parsing
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
    for entry in entries:
        if isinstance(entry, str):
            images[entry] = f"{DEST_REGISTRY}/{entry}"
        elif isinstance(entry, dict):
            for source, opts in entry.items():
                opts = opts or {}
                target = opts.get("target", f"{DEST_REGISTRY}/{source}")
                images[source] = target
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


def _version_sort_key(tag: str):
    """Sort key that orders version-like tags so the highest version comes last."""
    parts = re.split(r"[.\-+]", tag.lstrip("v"))
    result = []
    for p in parts:
        try:
            result.append((0, int(p)))
        except ValueError:
            result.append((1, p))
    return result


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

    clean_re = re.compile(r"^v?\d+(\.\d+)+$")
    simple_suffix_re = re.compile(r"^v?\d+(\.\d+)+-[a-zA-Z][a-zA-Z0-9]*$")

    clean = sorted([t for t in all_tags if clean_re.match(t)],
                   key=_version_sort_key,
                   reverse=True)
    suffixed = sorted(
        [t for t in all_tags if simple_suffix_re.match(t) and t not in clean],
        key=_version_sort_key,
        reverse=True)

    candidates = clean[:30] + suffixed[:20]
    if not candidates:
        return ""

    def _check(tag: str):
        try:
            d = resolve_digest(f"{repo}:{tag}", tool)
            return tag, d
        except Exception as exc:
            print(f"    [debug] Could not resolve {repo}:{tag}: {exc}",
                  file=sys.stderr)
            return tag, None

    futures = {pool.submit(_check, tag): tag for tag in candidates}
    matches = []
    for future in as_completed(futures):
        tag, d = future.result()
        if d == digest:
            matches.append(tag)

    if not matches:
        return ""

    clean_matches = [t for t in matches if clean_re.match(t)]
    if clean_matches:
        clean_matches.sort(key=_version_sort_key, reverse=True)
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
    with open(path, "w") as f:
        f.write("# Auto-generated by: uv run bin/mirror_images.py lock\n")
        f.write("# Do not edit manually.\n")
        yaml.dump(lock_data, f, default_flow_style=False, sort_keys=False)


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
        except Exception as exc:
            print(
                f"WARNING: Could not parse existing lock file {output_path}: {exc}",
                file=sys.stderr)
            print("Resolving all images from scratch.", file=sys.stderr)

    # Only resolve images not already in the lock file
    to_resolve = {
        src: tgt
        for src, tgt in images.items() if src not in results
    }
    if not to_resolve:
        print(f"All {len(images)} images already resolved in {output_path}")
        _write_lock_file(output_path,
                         {k: results[k]
                          for k in results if k in images})
        return 0

    print(
        f"Resolving {len(to_resolve)} of {len(images)} images using {tool} (parallelism={args.jobs})..."
    )

    errors = []
    lock = threading.Lock()

    def _resolve(source: str, target: str, pool: ThreadPoolExecutor):
        digest = resolve_digest(source, tool)
        normalized = parse_image_ref(source)
        tag = normalized.rsplit(":", 1)[1]

        if tag == "latest":
            specific = find_specific_tag(source, digest, tool, pool)
            resolved_tag = specific if specific else tag
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
                results[source] = {
                    "digest": digest,
                    "target": target,
                    "tag": tag
                }
                tag_info = f" (tag: {tag})" if tag != source.rsplit(
                    ":", 1)[-1] else ""
                print(f"  {source}: {digest}{tag_info}")
                # Snapshot progress after each successful resolve
                with lock:
                    _write_lock_file(
                        output_path,
                        {k: results[k]
                         for k in results if k in images})
            except Exception as exc:
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
    print(
        f"Checking {len(images)} images against target registry using {digest_tool}..."
    )

    # Phase 1: check which images need copying
    print(
        f"Phase 1: checking which images need copying ({args.jobs} workers)..."
    )

    def _check(source: str, info: dict):
        target = info["target"]
        digest = info["digest"]
        # Check if digest already exists at target by referencing target@digest
        target_repo = target.rsplit(":", 1)[0] if ":" in target else target
        ref = f"{target_repo}@{digest}"
        print(f"  ? {source} -> {target}")
        present = check_digest_exists(ref, digest_tool)
        return source, info, present

    to_copy = []
    already_present = 0
    checked = 0

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(_check, src, info): src
            for src, info in images.items()
        }
        for future in as_completed(futures):
            src = futures[future]
            checked += 1
            try:
                source, info, present = future.result()
            except Exception as exc:
                print(f"  ERROR checking {src}: {exc}", file=sys.stderr)
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

    def _is_external(img: str) -> bool:
        if img.startswith("$") or img.startswith("{"):
            return False
        if any(img.startswith(p) for p in internal_prefixes):
            return False
        if img.startswith("nginx-datadog-test-"):
            return False
        if not re.match(r"^[a-zA-Z0-9]", img):
            return False
        return True

    def _read_lines(filepath):
        try:
            with open(filepath) as f:
                return list(enumerate(f, 1))
        except OSError as exc:
            print(f"  [warning] Could not read {filepath}: {exc}",
                  file=sys.stderr)
            return []

    def _scan_dockerfiles():
        for filepath in glob.glob(os.path.join(PROJECT_DIR, "**/Dockerfile*"),
                                  recursive=True):
            if "/.git/" in filepath:
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
        for pattern in ("**/*.yml", "**/*.yaml"):
            for filepath in glob.glob(os.path.join(PROJECT_DIR, pattern),
                                      recursive=True):
                if "/.git/" in filepath or "/.gitlab/" in filepath:
                    continue
                for lineno, line in _read_lines(filepath):
                    m = re.match(r"^\s+image:\s+['\"]?(\S+?)['\"]?\s*$", line)
                    if m and _is_external(m.group(1)):
                        hits.append(
                            (filepath, lineno, line.rstrip(), m.group(1)))

    def _scan_ci_matrices():
        for filepath in glob.glob(
                os.path.join(PROJECT_DIR, ".gitlab", "build-and-test-*.yml")):
            for lineno, line in _read_lines(filepath):
                m = re.search(r'BASE_IMAGE:\s*\[(.+)\]', line)
                if m:
                    for img_m in re.finditer(r'"([^"]+)"', m.group(1)):
                        img = img_m.group(1)
                        if _is_external(img):
                            hits.append((filepath, lineno, line.rstrip(), img))

    _scan_dockerfiles()
    _scan_yaml_image_fields()
    _scan_ci_matrices()
    return hits


def cmd_lint(args: argparse.Namespace) -> int:
    """Check that all images are referenced from registry.ddbuild.io."""
    declared = parse_mirror_yaml(args.mirror_yaml)

    public_refs = find_public_image_refs()

    if not public_refs:
        print("All image references use registry.ddbuild.io.")
        return 0

    by_image: dict[str, list[tuple[str, int, str]]] = {}
    for filepath, lineno, line, img in public_refs:
        by_image.setdefault(img, []).append((filepath, lineno, line))

    undeclared = []
    print(
        "Public image references found (should use registry.ddbuild.io mirror):\n"
    )
    for img in sorted(by_image):
        target = declared.get(img)
        if target:
            replacement = target
        else:
            replacement = f"{DEST_REGISTRY}/{img}"
            undeclared.append(img)

        print(f"  {img}")
        print(f"    -> {replacement}")
        for filepath, lineno, line in by_image[img]:
            relpath = os.path.relpath(filepath, PROJECT_DIR)
            print(f"       {relpath}:{lineno}")
        print()

    if undeclared:
        print("Images not declared in mirror_images.yaml (add them first):")
        for img in sorted(undeclared):
            print(f"  - {img}")
        print()

    print(
        f"{len(public_refs)} public image reference(s) across {len(by_image)} image(s)."
    )
    return 1


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
