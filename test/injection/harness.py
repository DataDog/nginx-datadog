"""A private Linux host, Docker daemon, and trace collectors."""

import json
import os
from pathlib import Path
import shlex
import subprocess
import time
import uuid

import yaml

ROOT = Path(__file__).resolve().parents[2]
WORKLOADS = Path(__file__).with_name("workloads")
NGINX_VERSION = "1.31.5"
INJECTOR_VERSION = "0.71.0-1"
NGINX_PACKAGE_VERSION = "1.23.0-1"
PYTHON_PACKAGE_VERSION = "4.14.0-1"
AGENT_IMAGE = "ghcr.io/datadog/dd-apm-test-agent/ddapm-test-agent:v1.65.0"
UNSUPPORTED_IMAGE = "nginx:1.20.2"
MODES = ("host", "docker-debian", "docker-alpine")
STABLE_CONFIG_PATHS = (
    "/etc/datadog-agent/application_monitoring.yaml",
    "/etc/datadog-agent/managed/datadog-agent/stable/application_monitoring.yaml",
)
LABEL = "com.datadog.nginx-injection"


def poll(action, description, timeout=30):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            result = action()
            if result:
                return result
        except (AssertionError, RuntimeError) as error:
            last_error = error
        time.sleep(0.5)
    raise AssertionError(f"Timed out waiting for {description}: {last_error}")


def flatten(chunks):
    if isinstance(chunks, dict):
        assert "span_id" in chunks, f"Unexpected trace object: {chunks}"
        return [chunks]
    assert isinstance(chunks, list), f"Unexpected trace collection: {chunks}"
    return [span for chunk in chunks for span in flatten(chunk)]


def matching(spans, uri, service=None):
    return [
        span for span in spans
        if uri in span.get("meta", {}).get("http.url", "") and (
            service is None or span["service"] == service)
    ]


def trace_id(span):
    low = int(span["trace_id"])
    high = span.get("meta", {}).get("_dd.p.tid", "0")
    return (int(high, 16) << 64) | low


class Docker:

    def __init__(self, artifacts, context=None):
        context = context or os.environ.get("DOCKER_CONTEXT")
        host = os.environ.get("DOCKER_HOST")
        if not context and not host:
            context = subprocess.check_output(["docker", "context", "show"],
                                              text=True).strip()
        self.prefix = ["docker", "--context", context
                       ] if context else ["docker", "--host", host]
        self.artifacts = Path(artifacts).resolve()
        self.artifacts.mkdir(parents=True, exist_ok=True)

    def run(self, *args, check=True, timeout=300, input=None):
        command = [*self.prefix, *map(str, args)]
        try:
            result = subprocess.run(command,
                                    input=input,
                                    text=True,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE,
                                    timeout=timeout)
        except subprocess.TimeoutExpired as error:
            with (self.artifacts / "commands.log").open("a") as log:
                log.write(
                    f"$ {shlex.join(command)}\nTimed out after {timeout}s\n")
                for output in (error.stdout, error.stderr):
                    log.write(
                        output.decode(errors="replace") if isinstance(
                            output, bytes) else output or "")
            raise RuntimeError(
                f"Command timed out; see {self.artifacts / 'commands.log'}"
            ) from error
        with (self.artifacts / "commands.log").open("a") as log:
            log.write(
                f"$ {shlex.join(command)}\n{result.stdout}{result.stderr}\n")
        if check and result.returncode:
            raise RuntimeError(
                f"{shlex.join(command)} failed ({result.returncode}):\n"
                f"{(result.stdout + result.stderr)[-6000:]}\n"
                f"Full output: {self.artifacts / 'commands.log'}")
        return result.stdout.strip() if check else result

    def transfer(self, image, container):
        with (self.artifacts / "image-transfer.log").open("a") as log:
            save = subprocess.Popen([*self.prefix, "save", image],
                                    stdout=subprocess.PIPE,
                                    stderr=log)
            try:
                loaded = subprocess.run(
                    [*self.prefix, "exec", "-i", container, "docker", "load"],
                    stdin=save.stdout,
                    stdout=log,
                    stderr=log,
                    timeout=300)
                save.stdout.close()
                saved = save.wait(timeout=30)
                assert saved == 0, f"docker save failed for {image}: {saved}"
                assert loaded.returncode == 0, f"docker load failed for {image}"
            finally:
                save.stdout.close()
                if save.poll() is None:
                    save.kill()
                    save.wait()


class Images:

    def __init__(self, docker):
        self.docker = docker
        self.host = f"nginx-injection-host:{uuid.uuid4().hex[:12]}"
        self.loaded = set()
        info = json.loads(docker.run("info", "--format", "{{json .}}"))
        self.arch = {
            "aarch64": "arm64",
            "x86_64": "amd64"
        }.get(info["Architecture"], info["Architecture"])
        assert self.arch in ("arm64", "amd64"), info["Architecture"]
        expected = os.environ.get("INJECTION_ARCH")
        assert not expected or self.arch == expected, (self.arch, expected)
        docker.run("build",
                   "--platform",
                   f"linux/{self.arch}",
                   "--pull",
                   "--label",
                   LABEL,
                   "-t",
                   self.host,
                   WORKLOADS,
                   timeout=600)
        try:
            self.record(self.host)
            for image in (f"nginx:{NGINX_VERSION}",
                          f"nginx:{NGINX_VERSION}-alpine", AGENT_IMAGE):
                self.pull(image)
        except BaseException:
            self.close()
            raise

    def record(self, image):
        self.loaded.add(image)
        data = [
            json.loads(self.docker.run("image", "inspect", name))[0]
            for name in sorted(self.loaded)
        ]
        (self.docker.artifacts / "images.json").write_text(
            json.dumps(data, indent=2))

    def pull(self, image):
        if image not in self.loaded:
            self.docker.run("pull", "--platform", f"linux/{self.arch}", image)
            self.record(image)

    def close(self):
        self.docker.run("image", "rm", self.host)


class Sandbox:

    def __init__(self, images, mode, artifacts, package=None):
        self.images = images
        self.docker = images.docker
        self.mode = mode
        self.package = package
        self.artifacts = Path(artifacts)
        self.artifacts.mkdir(parents=True, exist_ok=True)
        self.name = f"nginx-injection-{uuid.uuid4().hex[:12]}"
        self.container = None
        self.installed = False
        self.workload = None
        self.collectors = []
        self.transferred = set()

    def start(self):
        self.container = self.docker.run("run", "-d", "--platform",
                                         f"linux/{self.images.arch}",
                                         "--privileged", "--label", LABEL,
                                         "--name", self.name, "-v",
                                         "/var/lib/docker", self.images.host)
        self.host("sh",
                  "-c", "dockerd --host=unix:///var/run/docker.sock "
                  "--storage-driver=overlay2 > /evidence/dockerd.log 2>&1",
                  detach=True)
        poll(lambda: self.inner("info", check=False).returncode == 0,
             "private Docker daemon")
        self.load(AGENT_IMAGE)
        for name, port, directory in (("a", 8126, "/var/run/datadog"),
                                      ("b", 9126, "/var/run/datadog-b")):
            self.inner("run", "-d", "--runtime=runc", "--network=host",
                       "--name", f"collector-{name}", "-v",
                       f"{directory}:{directory}", "-e", f"PORT={port}", "-e",
                       "DD_AGENT_URL=", "-e",
                       "DISABLE_LLMOBS_DATA_FORWARDING=true", "-e",
                       "DD_INSTRUMENT_SERVICE_WITH_APM=false", "-e",
                       f"DD_APM_RECEIVER_SOCKET={directory}/apm.socket",
                       AGENT_IMAGE, "ddapm-test-agent", "--otlp-http-port",
                       str(port + 1), "--otlp-grpc-port", str(port + 2))
            self.collectors.append(name)
            poll(lambda: self.agent("/info", name), f"collector {name}")
        self.host("chmod", "777", "/var/run/datadog/apm.socket")
        return self

    def host(self, *command, env=None, detach=False, **kwargs):
        args = ["exec"]
        if detach:
            args.append("-d")
        if kwargs.get("input") is not None:
            args.append("-i")
        for key, value in (env or {}).items():
            if value is not None:
                args.extend(["-e", f"{key}={value}"])
        result = self.docker.run(*args, self.container, *command, **kwargs)
        if command[0] == "datadog-installer" or command[:1] == ("bash", ):
            with (self.artifacts / "installer.log").open("a") as log:
                log.write(f"$ {shlex.join(command)}\n{result}\n")
        return result

    def inner(self, *command, **kwargs):
        return self.host("docker",
                         *command,
                         env={"DD_INSTRUMENT_SERVICE_WITH_APM": "false"},
                         **kwargs)

    def load(self, image):
        if image not in self.transferred:
            self.docker.transfer(image, self.container)
            self.transferred.add(image)

    def agent(self, path, collector="a"):
        port = 8126 if collector == "a" else 9126
        data = self.host("curl", "--fail", "--silent", "--show-error",
                         "--max-time", "3", f"http://127.0.0.1:{port}{path}")
        return json.loads(data)

    def spans(self, collector="a"):
        return flatten(self.agent("/test/traces", collector))

    def assert_injection_telemetry(self):

        def received():
            return [
                event for event in self.agent("/test/apmtelemetry")
                if event.get("application", {}).get("language_name") == "nginx"
                and any(
                    series.get("metric") == "inject.success"
                    for series in event.get("payload", {}).get("series", []))
            ]

        return poll(received, "Nginx injection success telemetry")

    def wait_spans(self, uri, service, collector="a", count=1):

        def received():
            spans = matching(self.spans(collector), uri, service)
            return spans if len(spans) >= count else None

        return poll(received,
                    f"{service} spans for {uri} at collector {collector}")

    def quiet(self, uri, service=None, collector="a", seconds=3):
        assert self.workload is None or not self.workload.running, "Flush workloads first"
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            assert self.agent("/info", collector)
            assert not matching(self.spans(collector), uri, service)
            time.sleep(0.5)

    def install(self, nginx=True):
        method = "host" if self.mode == "host" else "docker"
        environment = {
            "DD_APM_INSTRUMENTATION_ENABLED": method,
            "DD_APM_INSTRUMENTATION_LIBRARIES": "none",
            "DD_NO_AGENT_INSTALL": "true",
            "DD_INSTALL_ONLY": "true",
            "DD_INSTRUMENTATION_TELEMETRY_ENABLED": "false",
        }
        script = "/evidence/install-agent.sh"
        self.host(
            "curl", "-fsSL", "--retry", "3",
            "https://install.datadoghq.com/scripts/install_script_agent7.sh",
            "-o", script)
        self.host("bash", script, env=environment, timeout=300)
        self.host("ln", "-sf",
                  "/opt/datadog-packages/run/datadog-installer-ssi",
                  "/usr/local/bin/datadog-installer")
        self.host("datadog-installer",
                  "remove",
                  "datadog-apm-inject",
                  env=environment)
        self.install_published("apm-library-python-package",
                               PYTHON_PACKAGE_VERSION, environment)
        if nginx:
            self.install_nginx(environment)
        self.install_published("apm-inject-package", INJECTOR_VERSION,
                               environment)
        version = self.host("datadog-installer", "version")
        (self.artifacts / "installer-version.txt").write_text(version)
        if method == "host":
            preload = self.host("cat", "/etc/ld.so.preload")
            assert "/opt/datadog-packages/" in preload or "/opt/datadog/" in preload, preload
            assert "launcher.preload.so" in preload, preload
        else:
            poll(
                lambda: self.inner("info", "--format", "{{.DefaultRuntime}}")
                == "dd-shim", "installer-configured default dd-shim runtime")
        self.installed = True

    def install_published(self, repository, version, environment):
        registry = "install.datadoghq.com"
        self.host("curl", "-fsSL", "--retry", "3",
                  f"https://{registry}/v2/{repository}/manifests/{version}",
                  "-o", f"/evidence/{repository}-index.json")
        digest = self.host("sha256sum",
                           f"/evidence/{repository}-index.json").split()[0]
        data = json.loads(
            self.host("cat", f"/evidence/{repository}-index.json"))
        selected = [
            entry for entry in data["manifests"] if entry["platform"] == {
                "architecture": self.images.arch,
                "os": "linux"
            }
        ]
        assert len(selected) == 1, data
        (self.artifacts / f"{repository}.json").write_text(
            json.dumps(
                {
                    "version": version,
                    "index_digest": f"sha256:{digest}",
                    "manifest": selected[0]
                },
                indent=2))
        self.host("datadog-installer",
                  "install",
                  f"oci://{registry}/{repository}@sha256:{digest}",
                  env=environment)

    def install_nginx(self, environment):
        if self.package:
            from .package import module_checksum
            expected = module_checksum(Path(self.package), self.images.arch)
            self.host("mkdir", "-p", "/tmp/nginx-package")
            self.docker.run("cp", self.package,
                            f"{self.container}:/tmp/nginx-package.tar")
            self.host("tar", "-xf", "/tmp/nginx-package.tar", "-C",
                      "/tmp/nginx-package")
            environment = {
                **environment, "DD_INSTALLER_REGISTRY_URL":
                "file:///tmp/nginx-package"
            }
            url = "file:///tmp/nginx-package"
            self.host("datadog-installer", "install", url, env=environment)
        else:
            self.install_published("apm-library-nginx-package",
                                   NGINX_PACKAGE_VERSION, environment)
        installed = self.host(
            "readlink", "-f",
            "/opt/datadog-packages/datadog-apm-library-nginx/stable")
        checksum = self.host(
            "sha256sum",
            f"{installed}/nginx/{NGINX_VERSION}/ngx_http_datadog_module.so")
        (self.artifacts / "module-checksum.txt").write_text(checksum)
        if self.package:
            assert checksum.split()[0] == expected, (checksum, expected)
        self.host("sh", "-c",
                  "find /opt/datadog-packages -name '*.db' -o -name '*json'")

    def write_stable_config(self, path, configuration):
        content = yaml.safe_dump(configuration, sort_keys=True)
        self.host("mkdir", "-p", str(Path(path).parent))
        self.host("sh", "-c", 'cat > "$1"', "sh", path, input=content)
        name = path.removeprefix("/").replace("/", "-")
        (self.artifacts / name).write_text(content)
        return content

    def save(self, name):
        target = self.artifacts / name
        target.mkdir(exist_ok=True)
        self.docker.run("cp", f"{self.container}:/evidence", target)
        if self.workload and self.workload.running and self.mode != "host":
            for container in self.workload.containers:
                logs = self.inner("logs", container, check=False)
                (target / f"{container}.log").write_text(logs.stdout +
                                                         logs.stderr)
        for collector in self.collectors:
            logs = self.inner("logs", f"collector-{collector}", check=False)
            (target / f"{collector}.log").write_text(logs.stdout + logs.stderr)
        for collector in self.collectors:
            for endpoint, filename in (("/test/traces", "traces"),
                                       ("/test/apmtelemetry",
                                        "telemetry"), ("/info", "health")):
                (target / f"{collector}-{filename}.json").write_text(
                    json.dumps(self.agent(endpoint, collector), indent=2))
        (target / "daemon.json").write_text(
            self.host("cat", "/etc/docker/daemon.json", check=False).stdout
            or "{}")
        (target / "docker-info.json").write_text(
            self.inner("info", "--format", "{{json .}}"))

    def close(self):
        if not self.container:
            return
        try:
            try:
                if self.workload:
                    self.workload.stop()
            finally:
                self.save("final")
        finally:
            self.docker.run("rm", "-f", "-v", self.container)
            self.container = None


class Workload:

    def __init__(self,
                 sandbox,
                 name,
                 nginx_env=None,
                 backend_env=None,
                 image=None):
        self.sandbox = sandbox
        self.name = name
        self.directory = sandbox.artifacts / name
        self.directory.mkdir(parents=True, exist_ok=True)
        self.nginx_env = {"DD_SERVICE": "injection-nginx", **(nginx_env or {})}
        self.backend_env = {
            "DD_SERVICE": "injection-backend",
            **(backend_env or {})
        }
        self.image = image or f"nginx:{NGINX_VERSION}" + (
            "-alpine" if sandbox.mode == "docker-alpine" else "")
        self.running = False
        self.containers = []
        self.generation = 0
        sandbox.workload = self

    def start(self):
        assert not self.running
        self.running = True
        self.generation += 1
        sandbox = self.sandbox
        if sandbox.mode == "host":
            self.start_host("backend", [
                "/opt/flask/bin/gunicorn", "--chdir", "/workload", "--bind",
                "127.0.0.1:8081", "--workers", "1", "app:app"
            ], self.backend_env)
            self.start_host(
                "nginx",
                ["nginx", "-c", "/workload/nginx.conf", "-g", "daemon off;"],
                self.nginx_env)
        else:
            sandbox.load(sandbox.images.host)
            sandbox.load(self.image)
            self.start_container(
                "backend", sandbox.images.host, self.backend_env, [
                    "/opt/flask/bin/gunicorn", "--chdir", "/workload",
                    "--bind", "127.0.0.1:8081", "--workers", "1", "app:app"
                ])
            self.start_container(
                "nginx", self.image, self.nginx_env,
                ["nginx", "-c", "/workload/nginx.conf", "-g", "daemon off;"])
        poll(lambda: self.request("/ready", port=8081), "Flask readiness")
        poll(lambda: self.request("/ready"), "Nginx readiness")
        return self

    def environment(self, overrides):
        return {
            "DD_TRACE_AGENT_URL": "http://127.0.0.1:8126",
            "DD_APM_INSTRUMENTATION_DEBUG": "true",
            "DD_TRACE_STARTUP_LOGS": "false",
            **overrides
        }

    def start_host(self, service, command, overrides):
        script = f"echo $$ > /tmp/{service}-launcher.pid; exec {shlex.join(command)} " \
                 f"> /evidence/{service}.log 2>&1"
        self.sandbox.host("sh",
                          "-c",
                          script,
                          env=self.environment(overrides),
                          detach=True)

    def start_container(self, name, image, overrides, command):
        args = [
            "run", "-d", "--network=host", "--name", name, "-v",
            "/var/run/datadog:/var/run/datadog", "-v",
            "/workload:/workload:ro", "-v", "/srv/site:/srv/site:ro"
        ]
        for key, value in self.environment(overrides).items():
            if value is not None:
                args.extend(["-e", f"{key}={value}"])
        self.sandbox.inner(*args, image, *command)
        self.containers.append(name)

    def request(self, uri, headers=None, port=8080):
        if uri.startswith("/static/"):
            self.sandbox.host("sh", "-c", 'printf "injection test\\n" > "$1"',
                              "sh", f"/srv/site{uri}")
        args = [
            "curl", "--fail", "--silent", "--show-error", "--max-time", "3",
            "-w", "\n%{http_code}"
        ]
        for key, value in (headers or {}).items():
            args.extend(["-H", f"{key}: {value}"])
        response = self.sandbox.host(*args, f"http://127.0.0.1:{port}{uri}")
        body, _, status = response.rpartition("\n")
        assert int(status) == (204 if port == 8080 and uri == "/ready" else
                               200), response
        if uri != "/ready":
            with (self.directory / "requests.jsonl").open("a") as output:
                output.write(
                    json.dumps(
                        dict(uri=uri, port=port, status=int(status),
                             body=body)) + "\n")
        return body or status

    def evidence(self):
        data = json.loads(
            self.sandbox.host("python3", "/workload/processes.py"))
        assert data, "No running Nginx process"
        (self.directory / f"processes-{self.generation}.json").write_text(
            json.dumps(data, indent=2))
        return data

    def assert_module(self, loaded=True):
        processes = self.evidence()
        assert any("master process" in item["command"] for item in processes)
        assert any("worker process" in item["command"] for item in processes)
        for item in processes:
            assert len(item["modules"]) == int(loaded), item

    def assert_file(self, path, content):
        if self.sandbox.mode == "host":
            actual = self.sandbox.host("cat", path)
        else:
            actual = self.sandbox.inner("exec", "nginx", "cat", path)
        assert actual == content.rstrip(), actual

    def reload(self):
        before = self.evidence()
        master = next(item["pid"] for item in before
                      if "master process" in item["command"])
        workers = {
            item["pid"]
            for item in before if "worker process" in item["command"]
        }
        self.sandbox.host("kill", "-HUP", str(master))
        self.generation += 1
        poll(
            lambda: {
                item["pid"]
                for item in self.evidence()
                if "worker process" in item["command"]
            }.isdisjoint(workers), "new Nginx workers")

    def stop(self):
        if not self.running:
            return
        sandbox = self.sandbox
        self.evidence()
        if sandbox.mode == "host":
            sandbox.host("sh", "-c",
                         "kill -QUIT $(cat /tmp/injection-nginx.pid)")
            poll(
                lambda: not json.loads(
                    sandbox.host("python3", "/workload/processes.py")),
                "Nginx graceful shutdown")
            sandbox.host("sh", "-c",
                         "kill -TERM $(cat /tmp/backend-launcher.pid)")
            poll(
                lambda: sandbox.host(
                    "sh",
                    "-c",
                    "kill -0 $(cat /tmp/backend-launcher.pid)",
                    check=False).returncode != 0, "Flask graceful shutdown")
            for name in ("nginx", "backend"):
                self.directory.joinpath(
                    f"{name}-{self.generation}.log").write_text(
                        sandbox.host("cat", f"/evidence/{name}.log"))
        else:
            for name in reversed(self.containers):
                sandbox.inner("stop", "--signal",
                              "SIGQUIT" if name == "nginx" else "SIGTERM",
                              "--time", "30", name)
                state = json.loads(
                    sandbox.inner("inspect", "--format", "{{json .State}}",
                                  name))
                assert state["ExitCode"] == 0, state
                logs = sandbox.inner("logs", name, check=False)
                self.directory.joinpath(
                    f"{name}-{self.generation}.log").write_text(logs.stdout +
                                                                logs.stderr)
                sandbox.inner("rm", name)
            self.containers.clear()
        self.running = False

    def finish(self):
        self.stop()
        self.sandbox.save(self.name)


def unique_uri(proxy=False):
    return f"/{'proxy' if proxy else 'static'}/{uuid.uuid4().hex}"
