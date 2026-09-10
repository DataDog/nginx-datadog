import hashlib
import io
import json
import tarfile

import pytest
import zstandard

from test.injection.harness import flatten, matching, trace_id, NGINX_VERSION
from test.injection.package import module_checksum


def test_match_separate_trace_chunks():
    nginx = {
        "span_id": 2,
        "service": "nginx",
        "meta": {
            "http.url": "http://nginx/proxy/unique"
        }
    }
    backend = {
        "span_id": 3,
        "parent_id": 2,
        "service": "backend",
        "meta": {
            "http.url": "http://backend/proxy/unique"
        }
    }
    readiness = {
        "span_id": 4,
        "service": "nginx",
        "meta": {
            "http.url": "http://nginx/ready"
        }
    }
    spans = flatten([[nginx], [], [[backend]], [readiness]])
    assert matching(spans, "/proxy/unique", "nginx") == [nginx]
    assert matching(spans, "/proxy/unique", "backend") == [backend]
    assert matching(spans, "/missing") == []


def test_preserve_duplicate_spans():
    span = {"span_id": 1, "meta": {"http.url": "/static/unique"}}
    assert len(matching(flatten([[span], [span]]), "/static/unique")) == 2


@pytest.mark.parametrize("value",
                         [None, {
                             "error": "collector failed"
                         }, "invalid"])
def test_reject_invalid_collector_response(value):
    with pytest.raises(AssertionError):
        flatten(value)


def test_reconstruct_w3c_id():
    span = {
        "trace_id": 0x0123456789ABCDEF,
        "meta": {
            "_dd.p.tid": "fedcba9876543210"
        }
    }
    assert trace_id(span) == 0xFEDCBA98765432100123456789ABCDEF
    assert trace_id({"trace_id": 123}) == 123


def make_package(path, corrupt=False, compression="gzip"):
    blobs = {}

    def add(data):
        digest = hashlib.sha256(data).hexdigest()
        blobs[f"blobs/sha256/{digest}"] = data
        return {"digest": f"sha256:{digest}"}

    layer = io.BytesIO()
    with tarfile.open(fileobj=layer, mode="w:gz") as archive:
        entry = tarfile.TarInfo(
            f"nginx/{NGINX_VERSION}/ngx_http_datadog_module.so")
        entry.size = len(b"module")
        archive.addfile(entry, io.BytesIO(b"module"))
    layer_bytes = layer.getvalue()
    if compression == "zstd":
        import gzip
        layer_bytes = zstandard.ZstdCompressor().compress(
            gzip.decompress(layer_bytes))
    descriptor = add(layer_bytes)
    descriptor[
        "mediaType"] = "application/vnd.datadog.package.layer.v1.tar+" + compression
    manifest = add(json.dumps({"layers": [descriptor]}).encode())
    manifest["platform"] = {"os": "linux", "architecture": "arm64"}
    blobs["index.json"] = json.dumps({"manifests": [manifest]}).encode()
    if corrupt:
        blobs[next(iter(blobs))] = b"changed"
    with tarfile.open(path, "w") as archive:
        for name, data in blobs.items():
            entry = tarfile.TarInfo(name)
            entry.size = len(data)
            archive.addfile(entry, io.BytesIO(data))


@pytest.mark.parametrize("compression", ["gzip", "zstd"])
def test_package_module_checksum(tmp_path, compression):
    package = tmp_path / "package.tar"
    make_package(package, compression=compression)
    assert module_checksum(package,
                           "arm64") == hashlib.sha256(b"module").hexdigest()
    with pytest.raises(AssertionError, match="Linux amd64"):
        module_checksum(package, "amd64")


def test_reject_corrupt_oci_blob(tmp_path):
    package = tmp_path / "package.tar"
    make_package(package, corrupt=True)
    with pytest.raises(AssertionError):
        module_checksum(package, "arm64")
