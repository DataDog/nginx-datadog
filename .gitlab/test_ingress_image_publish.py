import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest


CONFIG = Path(__file__).with_name("ingress-image-publish.yml")
DIGEST = "sha256:" + "a" * 64
REGISTRY = "registry.example.test"
VERSIONS = ("1.14.5", "1.15.1")


def job_script(name):
    """Extract a hidden job's literal script block, not the YAML job."""
    lines = CONFIG.read_text().splitlines(keepends=True)
    start = lines.index(f"{name}:\n")
    end = next((index for index in range(start + 1, len(lines))
                if lines[index].strip() and not lines[index][0].isspace()
                and not lines[index].startswith("#")), len(lines))
    start = lines.index("    - |\n", start, end) + 1
    block = []
    for line in lines[start:end]:
        if line.strip() and not line.startswith("      "):
            break
        block.append(line)
    return textwrap.dedent("".join(block))


# Each mock records its arguments and can fail on a matching command and image.
MOCK = '''#!{python}
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
args = [name] + sys.argv[1:]
with open(os.environ["MOCK_LOG"], "a") as log:
    log.write(json.dumps([args, pathlib.Path("ingress-images.env").exists()]) + "\\n")
if (" ".join(args).startswith(os.environ.get("MOCK_FAIL", "\\0")) and
        os.environ.get("MOCK_FAIL_MATCH", "") in " ".join(args)):
    sys.exit(1)
if name == "crane":
    print({digest!r})
'''.format(python=sys.executable, digest=DIGEST)


class IngressImageJobsTest(unittest.TestCase):
    def setUp(self):
        """Create isolated artifacts and mock every external image command."""
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        for command in ("docker", "crane", "ddsign", "dd-pkg"):
            mock = self.bin / command
            mock.write_text(MOCK)
            mock.chmod(0o755)
        for version in VERSIONS:
            for arch in ("amd64", "arm64"):
                module = self.root / "artifacts-ingress" / arch / version
                module.mkdir(parents=True)
                (module / "ngx_http_datadog_module.so").touch()
        self.log = self.root / "commands.log"
        self.dotenv = self.root / "ingress-images.env"
        self.env = dict(os.environ, PATH=f"{self.bin}:/usr/bin:/bin",
                        MOCK_LOG=str(self.log), CI_REGISTRY=REGISTRY,
                        CI_JOB_ID="123", CI_COMMIT_TAG="v2.3.4",
                        CI_COMMIT_SHORT_SHA="abc123",
                        INGRESS_IMAGE_PUBLIC_REGISTRIES="dockerhub")
        for variable in ("INGRESS_IMAGE_TAGS", "MOCK_FAIL", "MOCK_FAIL_MATCH"):
            self.env.pop(variable, None)

    def run_job(self, name, **variables):
        return subprocess.run(["/bin/bash", "-c", job_script(name)], cwd=self.root,
                              env=dict(self.env, **variables), capture_output=True, text=True)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_builds_signs_and_emits_verified_refs(self):
        """Check both builds, the index, and signing order for each version."""
        result = self.run_job(".ingress-image-source")
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = []
        refs = []
        for version in VERSIONS:
            tag = f"v{version}-123"
            image = f"{REGISTRY}/ingress-nginx-injection:{tag}"
            refs.append(f"{tag}@{DIGEST}")
            for arch in ("amd64", "arm64"):
                expected.extend([
                    ["docker", "build", "--progress=plain", "--no-cache", "--provenance=false",
                     "--sbom=false", "--build-context", f"build=artifacts-ingress/{arch}/{version}",
                     "--platform", f"linux/{arch}", "--tag", f"{image}-{arch}",
                     "injection/ingress-nginx"],
                    ["docker", "push", f"{image}-{arch}"],
                ])
            expected.extend([
                ["docker", "buildx", "imagetools", "create", "--tag", image,
                 f"{image}-amd64", f"{image}-arm64"],
                ["crane", "digest", image],
                ["ddsign", "sign", f"{image}@{DIGEST}"],
                ["ddsign", "verify", "--roles=ci", f"{image}@{DIGEST}"],
            ])
        self.assertEqual(self.calls(), [[call, False] for call in expected])
        self.assertEqual(self.dotenv.read_text(), f"INGRESS_IMAGE_TAGS={' '.join(refs)}\n")

    def test_publish_release_and_dev_use_exact_digests(self):
        """Pass build output to each publish mode and check all destinations."""
        result = self.run_job(".ingress-image-source")
        self.assertEqual(result.returncode, 0, result.stderr)
        tags = self.dotenv.read_text().strip().split("=", 1)[1]
        for mode in ("release", "dev"):
            with self.subTest(mode=mode):
                self.log.unlink()
                result = self.run_job(".ingress-image-publish", INGRESS_IMAGE_TAGS=tags,
                                      INGRESS_IMAGE_RELEASE_MODE=mode)
                self.assertEqual(result.returncode, 0, result.stderr)
                expected = []
                for version in VERSIONS:
                    tag = f"v{version}"
                    if mode == "release":
                        destination = f"ingress-nginx-injection:{tag}-dd.v2.3.4,ingress-nginx-injection:{tag}"
                    else:
                        destination = f"ingress-nginx-injection-dev:{tag}-dev.abc123"
                    expected.append([["dd-pkg", "publish-image", "--sources",
                                      f"{REGISTRY}/ingress-nginx-injection:{tag}-123@{DIGEST}",
                                      "--merge-strategy", "none", "--destinations", destination,
                                      "--registries", "dockerhub"], True])
                self.assertEqual(self.calls(), expected)

    def test_failures_stop_without_dotenv(self):
        """A failure after an earlier version must not expose partial output."""
        for command in ("docker build", "ddsign sign", "ddsign verify"):
            with self.subTest(command=command):
                if self.log.exists():
                    self.log.unlink()
                result = self.run_job(".ingress-image-source", MOCK_FAIL=command,
                                      MOCK_FAIL_MATCH=VERSIONS[1])
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.dotenv.exists())
                calls = self.calls()
                self.assertTrue(" ".join(calls[-1][0]).startswith(command))
                self.assertIn(VERSIONS[1], " ".join(calls[-1][0]))
                self.assertTrue(all(not dotenv for _, dotenv in calls))

    def test_missing_arm64_stops_before_signing(self):
        module = self.root / "artifacts-ingress" / "arm64" / VERSIONS[0] / "ngx_http_datadog_module.so"
        module.unlink()
        result = self.run_job(".ingress-image-source")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.dotenv.exists())
        self.assertEqual([call[0][:2] for call in self.calls()],
                         [["docker", "build"], ["docker", "push"]])

    def test_missing_amd64_directory_stops_before_signing(self):
        shutil.rmtree(self.root / "artifacts-ingress" / "amd64" / VERSIONS[0])
        result = self.run_job(".ingress-image-source")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.dotenv.exists())
        self.assertEqual(self.calls(), [])

    def test_reruns_use_distinct_tags(self):
        """A new job ID must produce new source tags."""
        refs = []
        for job_id in ("123", "456"):
            if self.dotenv.exists():
                self.dotenv.unlink()
            result = self.run_job(".ingress-image-source", CI_JOB_ID=job_id)
            self.assertEqual(result.returncode, 0, result.stderr)
            refs.append(self.dotenv.read_text())
        self.assertNotEqual(*refs)
        self.assertIn(f"v{VERSIONS[0]}-123@{DIGEST}", refs[0])
        self.assertIn(f"v{VERSIONS[0]}-456@{DIGEST}", refs[1])

    def test_dependencies_cover_build_matrices(self):
        """Keep image inputs in step with the existing module build matrices."""
        config = CONFIG.read_text()
        for mode, job in (("all", "build-ingress-injection-images"),
                          ("fast", "build-ingress-injection-images-dev")):
            matrix = CONFIG.with_name(f"build-and-test-{mode}.yml").read_text()
            matrix = matrix.split(f"build-ingress-nginx-{mode}:\n", 1)[1].split("\nbuild-", 1)[0]
            versions = re.findall(r'^          - "([\d.]+)"$', matrix, re.MULTILINE)
            job_config = config.split(f"\n{job}:\n", 1)[1].split("\n  rules:", 1)[0]
            dependencies = re.findall(r'    - "(build-ingress-nginx-[^\"]+)"', job_config)
            expected = [f"build-ingress-nginx-{mode}: [{arch}, {version}]"
                        for arch in ("amd64", "arm64") for version in versions]
            self.assertTrue(versions)
            self.assertCountEqual(dependencies, expected)

    def test_publish_requires_signed_refs(self):
        result = self.run_job(".ingress-image-publish", INGRESS_IMAGE_RELEASE_MODE="release")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
