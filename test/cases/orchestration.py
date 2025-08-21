"""Service orchestration (docker compose) facilities for testing"""

from . import formats
from .lazy_singleton import LazySingleton

import tarfile
import tempfile
import contextlib
import faulthandler
import json
import os
from pathlib import Path
import queue
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import traceback
import uuid


# The name of the signal is "quit," but it doesn't mean quit.  Typically it
# means "dump a core file." Here we use it to mean "print python stacks for all
# threads."
def quit_signal_handler(signum, frame):
    faulthandler.dump_traceback()


def wait_until(predicate_func, timeout_seconds):
    """Wait until `predicate_func` is True."""
    before = time.monotonic()
    while True:
        if predicate_func():
            break

        now = time.monotonic()
        if now - before >= timeout_seconds:
            raise Exception(
                f"{timeout_seconds} seconds timeout exceeded while waiting for nginx workers to stop.  {now - before} seconds elapsed."
            )
        time.sleep(0.5)


signal.signal(signal.SIGQUIT, quit_signal_handler)

# Since we override the environment variables of child processes,
# `subprocess.Popen` (and its derivatives) need to know exactly where the
# "docker" executable is, since it won't find it in the passed-in env's PATH.
docker_command_path = shutil.which("docker")

# If we want to always pass some flags to `docker compose` or to `docker`, put
# them here.  For example, "--tls".  However, TLS behavior can be specified in
# environment variables.
docker_compose_flags = []
docker_flags = []

# "/datadog-tests" is a directory created by the docker build of the nginx test
# test image. It contains the module, the nginx config.
nginx_conf_path = "/datadog-tests/nginx.conf"


def docker_compose_command(*args):
    return [
        docker_command_path,
        "compose",
        "--ansi",
        "never",
        *docker_compose_flags,
        *args,
    ]


def docker_command(*args):
    return [docker_command_path, *docker_flags, *args]


# docker compose (at least the version running on my laptop) invokes `docker`
# unqualified, and so when we run `docker compose` commands, we have to do it
# in an environment where `docker_command_path` is in the PATH.
# See `child_env`.
docker_bin = str(Path(docker_command_path).parent)

# `sync_port` is the port that services will listen on for "sync" requests.
sync_port = 8888


def child_env(parent_env=None):
    if parent_env is None:
        parent_env = os.environ

    result = {}
    for name in ("BASE_IMAGE", "NGINX_CONF_PATH", "NGINX_MODULES_PATH"):
        if name in parent_env:
            result[name] = parent_env[name]

    # Forward DOCKER_HOST, DOCKER_TLS_VERIFY, etc.
    for name, value in parent_env.items():
        if name.startswith("DOCKER_"):
            result[name] = value

    # Forward COMPOSE_PROJECT_NAME (with default), COMPOSE_VERSION, etc.
    result["COMPOSE_PROJECT_NAME"] = parent_env.get("COMPOSE_PROJECT_NAME",
                                                    "test")
    for name, value in parent_env.items():
        if name.startswith("COMPOSE_"):
            result[name] = value

    result["PATH"] = docker_bin

    # `docker compose` uses $HOME to examine `~/.cache`, though I have no idea
    # why.
    result["HOME"] = parent_env["HOME"]

    return result


def to_service_name(container_name):
    # test_foo_bar_1 -> foo_bar
    #
    # Note that the "test_" prefix is the docker compose project name, which
    # you can override using the COMPOSE_PROJECT_NAME environment
    # variable.  We use that environment variable to hard-code the name to
    # "test".  Otherwise, it defaults to the basename of the directory.  Right
    # now those two are the same, but I don't want the location of these tests
    # to be able to break this.  See mention of COMPOSE_PROJECT_NAME in
    # `child_env()`.
    #
    # When I run docker compose locally on my machine, the parts of the
    # container name are separated by underscore ("_"), while when I run
    # docker compose in CircleCI, hyphen ("-") is used.  Go with whichever is
    # being used.
    if "_" in container_name and "-" in container_name:
        raise Exception(
            f"Container name {json.dumps(container_name)} contains both underscores and hyphens.  I can't tell which is being used as a delimiter."
        )

    if "_" in container_name:
        delimiter = "_"
    elif "-" in container_name:
        delimiter = "-"
    else:
        raise Exception(
            f"Container name {json.dumps(container_name)} contains neither underscores nor hyphens.  I don't know which delimiter to use."
        )

    return delimiter.join(container_name.split(delimiter)[1:-1])


def docker_compose_ps(service):
    command = docker_compose_command("ps", "--quiet", service)
    result = subprocess.run(command,
                            stdout=subprocess.PIPE,
                            env=child_env(),
                            encoding="utf8",
                            check=True)
    return result.stdout.strip()


def docker_top(container, verbose_output):
    # `docker top` is picky about the output format of `ps`.  It allows us to
    # pass arbitrary options to `ps`, but, for example, if `pid` is not first,
    # it breaks `docker top`.
    fields = ("pid", "cmd")

    command = docker_command("top", container, "-o", ",".join(fields))
    with print_duration("Consuming docker compose top PIDs", verbose_output):
        with subprocess.Popen(command,
                              stdout=subprocess.PIPE,
                              env=child_env(),
                              encoding="utf8") as child:
            # Discard the first line, which is the field names.
            # We could suppress it using ps's `--no-headers` option, but that
            # breaks something inside of `docker top`.
            next(child.stdout)
            # Yield the remaining lines as tuples of fields("cmd" contains spaces,
            # so this is particular to `fields`).
            for line in child.stdout:
                split = line.split()
                pid_str, cmd = split[0], " ".join(split[1:])
                yield (int(pid_str), cmd)


def nginx_worker_pids(nginx_container, verbose_output):
    # cmd could be "nginx: worker process" or "nginx: worker process shutting down".
    # We want to match both.
    return set(pid for pid, cmd in docker_top(nginx_container, verbose_output)
               if re.match(r"\s*nginx: worker process", cmd))


def docker_compose_ps_with_retries(max_attempts, service):
    assert max_attempts > 0
    while True:
        try:
            result = docker_compose_ps(service)
            if result == "":
                raise Exception(
                    f"docker_compose_ps({json.dumps(service)}) returned an empty string"
                )
            return result
        except Exception:
            max_attempts -= 1
            if max_attempts == 0:
                raise


def ready_services():
    """Return the set of names of services that are "ready"."""
    command = docker_compose_command("ps", "--services", "--filter",
                                     "status=running")
    result = subprocess.run(command,
                            stdout=subprocess.PIPE,
                            env=child_env(),
                            encoding="utf8",
                            check=True)
    return set(result.stdout.strip().split("\n"))


# When a function runs on its own thread, like `docker_compose_up`, exceptions
# that escape the function do not terminate the interpreter.  This wrapper
# calls the rather abrupt `os._exit` with a failure status code if the
# decorated function raises any exception.
def exit_on_exception(func):
    # Any nonzero value would do.
    status_code = 2

    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except:
            traceback.print_exc()
            os._exit(status_code)  # note: doesn't flush IO

    return wrapper


def remove_terminal_escapes(input_string):
    pattern = r"\x1b\[[0-9;]*[a-zA-Z]"
    cleaned_string = re.sub(pattern, "", input_string)
    return cleaned_string


@exit_on_exception
def docker_compose_up(on_ready, logs, verbose_file):
    """This function is meant to be executed on its own thread."""
    containers = {}  # {service: container_id}
    command = docker_compose_command("up", "--remove-orphans",
                                     "--force-recreate", "--no-color")
    before = time.monotonic()
    with subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=child_env(),
            encoding="utf8",
    ) as child:
        for line in child.stdout:
            line = remove_terminal_escapes(line)
            kind, fields = formats.parse_docker_compose_up_line(line)
            print(
                json.dumps([time.monotonic(), kind, fields]),
                file=verbose_file,
                flush=True,
            )

            if kind == "attach_to_logs":
                # Done starting containers.  Time to deliver the container IDs
                # to our caller.
                after = time.monotonic()
                print(
                    f"It took {after - before} seconds to create all service containers.",
                    file=verbose_file,
                    flush=True,
                )
                with print_duration("Waiting for services to be ready",
                                    verbose_file):
                    while len(ready_services()) != len(containers):
                        poll_seconds = 0.1
                        print(
                            "Not all services are ready.  Going to wait",
                            poll_seconds,
                            "seconds",
                            file=verbose_file,
                            flush=True,
                        )
                        time.sleep(poll_seconds)
                on_ready({"containers": containers})
            elif kind == "finish_create_container":
                # Started a container.  Add its container ID to `containers`.
                container_name = fields["container"]
                service = to_service_name(container_name)
                # Consult `docker compose ps` for the service's container ID.
                # Since we're handling a finish_create_container event, you'd
                # think that the container would be available now.  However,
                # there's a race here where docker compose does not yet know
                # which container corresponds to `service` (even though it just
                # told us that the container was created).
                #
                # Additionally, some `docker compose ps` implementations exit
                # with a nonzero (error) status when there is no container ID
                # to report, while others exit with status zero (success) and
                # don't print any output.  So, we retry (up to a limit) until
                # `docker compose ps` exits with status zero and produces
                # output.
                containers[service] = docker_compose_ps_with_retries(
                    max_attempts=100, service=service)
            elif kind == "service_log":
                # Got a line of logging from some service.  Push it onto the
                # appropriate queue for consumption by tests.
                service = fields["service"]
                payload = fields["payload"]
                logs[service].put(payload)


@contextlib.contextmanager
def print_duration(of_what, output):
    before = time.monotonic()
    yield
    after = time.monotonic()
    print(f"{of_what} took {after - before:.5} seconds.",
          file=output,
          flush=True)


def docker_compose_services():
    command = docker_compose_command("config", "--services")
    result = subprocess.run(command,
                            stdout=subprocess.PIPE,
                            env=child_env(),
                            encoding="utf8",
                            check=True)
    return result.stdout.split()


def curl(url, headers, stderr=None, method="GET", body=None, http_version=1):

    def header_args():
        if isinstance(headers, dict):
            for name, value in headers.items():
                yield "--header"
                yield f"{name}: {value}"
        else:
            for name, value in headers:
                yield "--header"
                yield f"{name}: {value}"

    if http_version == 1:
        version_arg = "--http1.1"
    elif http_version == 2:
        version_arg = "--http2-prior-knowledge"
    elif http_version == 3:
        version_arg = "--http3-only"
    else:
        raise Exception(f"Unknown HTTP version: {http_version}")

    # "curljson.sh" is a script that lives in the "client" docker compose
    # service.  It's a wrapper around "curl" that outputs a JSON object of
    # information on the first line, and outputs a JSON string of the response
    # body on the second line.  See the documentation of the "json" format for
    # the "--write-out" option in curl's manual for more information on the
    # properties of the JSON object.

    if body is None:
        body_args = []
    else:
        # read data from stdin
        body_args = ["--data-binary", "@-"]

    if method == "HEAD":
        method_args = ["--head"]
    else:
        method_args = [f"-X{method}"]

    # "-T" means "don't allocate a TTY".  This prevents `jq` from outputting in
    # color.
    command = docker_compose_command(
        "exec",
        "-T",
        "--",
        "client",
        "curljson.sh",
        *method_args,
        *header_args(),
        "-k",
        version_arg,
        *body_args,
        url,
    )
    result = subprocess.run(
        command,
        input=body if body is not None else "",
        stdout=subprocess.PIPE,
        stderr=stderr,
        env=child_env(),
        encoding="utf8",
        check=True,
    )
    fields_json, headers_json, body_json, *rest = result.stdout.split("\n")
    if any(line for line in rest):
        raise Exception("Unexpected trailing output to curljson.sh: " +
                        json.dumps(rest))

    fields = json.loads(fields_json)
    headers = json.loads(headers_json)
    body = json.loads(body_json)
    return fields, headers, body


def add_services_in_nginx_etc_hosts(services):
    """When we added build/test support on ARM64 by using the ARM64 execution
    environment in CircleCI, we started seeing intermittent delays in DNS
    lookups, always about five seconds.

    This function uses `getent` to look up the IP address of each `docker
    compose` service, and then associates the address with the service name by
    adding a line to /etc/hosts in the nginx service container. This way, the
    default `gethostbyname()` resolver used by nginx will not use DNS."""
    script = f"""
    for service in {" ".join(f"'{service}'" for service in services)}; do
        getent hosts "$service" >>/etc/hosts
    done
    """
    # "-T" means "don't allocate a TTY".  This is necessary to avoid the
    # error "the input device is not a TTY".
    command = docker_compose_command("exec", "-T", "--", "nginx", "/bin/sh")
    subprocess.run(command, input=script, env=child_env(), encoding="utf8")


class Orchestration:
    """A handle for a `docker compose` session.

    `up()` runs `docker compose up` and spawns a thread that consumes its
    output.

    `down()` runs `docker compose down`.

    Other methods perform integration test specific actions.

    This class is meant to be accessed through the `singleton()` function of
    this module, not instantiated directly.
    """

    def __init__(self):
        project_name = child_env()["COMPOSE_PROJECT_NAME"]
        self.verbose = (Path(__file__).parent.resolve().parent /
                        f"logs/{project_name}.log").open("w")

        print(
            "The test runner is running in the following environment:",
            os.environ,
            file=self.verbose,
        )

    # Properties (all private)
    # - `up_thread` is the `threading.Thread` running `docker compose up`.
    # - `logs` is a `dict` that maps service name to a `queue.SimpleQueue` of log
    #   lines.
    # - `containers` is a `dict` {service: container ID} that, per service,
    #   maps to the Docker container ID in which the service is running.
    # - `services` is a `list` of service names as defined in the
    #   `docker compose` config.
    # - `verbose` is a file-like object to which verbose logging is written.

    def up(self):
        """Start service orchestration.

        Run `docker compose up` to bring up the orchestrated services.  Begin
        parsing their logs on a separate thread.
        """
        # Before we bring things up, first clean up any detritus left over from
        # previous runs.  Failing to do so can create problems later when we
        # ask docker compose which container a service is running in.
        command = docker_compose_command("down", "--remove-orphans")
        subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=self.verbose,
            stderr=self.verbose,
            env=child_env(),
            check=True,
        )

        self.services = docker_compose_services()
        print("services:", self.services, file=self.verbose, flush=True)
        self.logs = {service: queue.SimpleQueue() for service in self.services}
        ready_queue = queue.SimpleQueue()
        self.up_thread = threading.Thread(target=docker_compose_up,
                                          args=(ready_queue.put, self.logs,
                                                self.verbose))
        self.up_thread.start()
        runtime_info = ready_queue.get()
        self.containers = runtime_info["containers"]
        print(runtime_info, file=self.verbose, flush=True)
        add_services_in_nginx_etc_hosts(self.services)

    def down(self):
        """Stop service orchestration.

        Run `docker compose down` to bring down the orchestrated services.
        Join the log-parsing thread.
        """
        if self.has_coverage_data():
            self.create_coverage_tarball()

        command = docker_compose_command("down", "--remove-orphans")
        with print_duration("Bringing down all services", self.verbose):
            with subprocess.Popen(
                    command,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE,  # "Stopping test_foo_1   ... done"
                    env=child_env(),
                    encoding="utf8",
            ) as child:
                for line in child.stderr:
                    kind, fields = formats.parse_docker_compose_down_line(line)
                    print(json.dumps((kind, fields)),
                          file=self.verbose,
                          flush=True)

            self.up_thread.join()

        self.verbose.close()

    @staticmethod
    def has_coverage_data():
        command = docker_compose_command("exec", "-T", "--", "nginx", "find",
                                         "/tmp", "-name", "*.profraw")
        result = subprocess.run(command, capture_output=True)
        if result.returncode != 0:
            return False

        return len(result.stdout)

    @staticmethod
    def create_coverage_tarball():
        cmd = docker_compose_command(
            "exec",
            "-T",
            "--",
            "nginx",
            "find",
            "/tmp",
            "-name",
            "*.profraw",
        )

        result = subprocess.run(cmd, capture_output=True)
        if result.returncode != 0:
            raise Exception("Failed to create tarball")

        with tempfile.TemporaryDirectory() as work_dir:
            files = []
            for src_profraw in result.stdout.decode().split():
                out_profraw = os.path.join(work_dir,
                                           os.path.basename(src_profraw))
                cp_cmd = docker_compose_command("cp", f"nginx:{src_profraw}",
                                                out_profraw)
                subprocess.run(cp_cmd, stdout=subprocess.DEVNULL)
                files.append(out_profraw)

            with tarfile.open("./coverage_data.tar.gz", "w:gz") as tar:
                for f in files:
                    tar.add(f, arcname=os.path.basename(f))

    @staticmethod
    def nginx_version():
        result = subprocess.run(
            docker_compose_command("exec", "--", "nginx", "nginx", "-v"),
            capture_output=True,
            text=True,
            check=True,
        )
        match = re.search(r"/([\d.]+)", result.stderr)
        return match.group(1) if match else None

    def send_nginx_websocket_request(
        self,
        path,
        body,
        port=80,
        tls=False,
        stderr=None,
    ):
        """Send a websocket request to nginx, and return the resulting HTTP
        status code and response body as a tuple `(status, body)`.
        """
        protocol = "wss" if tls else "ws"
        url = f"{protocol}://nginx:{port}{path}"
        print("connecting to websocket", url, file=self.verbose, flush=True)

        command = docker_compose_command("exec", "-T", "--", "client",
                                         "websocat", "-k", url)
        result = subprocess.run(
            command,
            input=body,
            stdout=subprocess.PIPE,
            stderr=stderr,
            env=child_env(),
            encoding="utf8",
            check=True,
        )
        return result.returncode, result.stdout

    def send_nginx_raw_http_request(self, port=80, request_line=""):
        """Send the request line to nginx, and return the resulting HTTP
        status code and response body as a tuple `(status, body)`.
        """
        command = docker_compose_command(
            "exec",
            "-T",
            "--",
            "client",
            "nc",
            "nginx",
            str(port),
        )

        result = subprocess.run(
            command,
            input=request_line,
            stdout=subprocess.PIPE,
            env=child_env(),
            encoding="utf8",
            check=True,
        )
        return result.returncode, result.stdout

    def send_nginx_http_request(
        self,
        path,
        port=80,
        headers={},
        method="GET",
        req_body=None,
        http_version=1,
        tls=False,
    ):
        """Send a "GET <path>" request to nginx, and return the resulting HTTP
        status code and response body as a tuple `(status, body)`.
        """
        protocol = "https" if tls else "http"
        url = f"{protocol}://nginx:{port}{path}"
        print("fetching", url, file=self.verbose, flush=True)
        fields, headers, body = curl(
            url,
            headers,
            body=req_body,
            stderr=self.verbose,
            method=method,
            http_version=http_version,
        )
        return fields["response_code"], headers, body

    def setup_remote_config_payload(self, payload):
        """Sets up the next remote config response"""
        url = f"http://agent:8126/save_rem_cfg_resp"
        print("posting", url, file=self.verbose, flush=True)
        fields, headers, body = curl(url, {},
                                     body=payload,
                                     stderr=self.verbose,
                                     method="POST")
        return fields["response_code"], headers, body

    def send_nginx_grpc_request(self, symbol, port=1337):
        """Send an empty gRPC request to the nginx endpoint at "/", where
        the gRPC request is named by `symbol`, which has the form
        "package.service.request".  The request is made using the `grpcurl`
        command line utility.  Return the exit status of `grpcurl` and its
        combined stdout and stderr as a tuple `(status, output)`.
        """
        address = f"nginx:{port}"
        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".
        command = docker_compose_command("exec", "-T", "--", "client",
                                         "grpcurl", "-plaintext", address,
                                         symbol)
        result = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf8",
            env=child_env(),
        )
        return result.returncode, result.stdout

    def sync_service(self, service):
        """Establish synchronization points in the logs of a service.

        Send a "sync" request to the specified `service`,
        and wait for the corresponding log messages to appear in the
        docker compose log.  This way, we know that whatever we have done
        previously has already appeared in the log.

            log_lines = orch.sync_service('agent')

        where `log_lines` is a chronological list of strings, each a line from
        the service's log.
        """
        token = str(uuid.uuid4())
        fields, _, body = curl(
            f"http://{service}:{sync_port}",
            headers={"X-Datadog-Test-Sync-Token": token},
        )

        assert fields["response_code"] == 200

        log_lines = []
        q = self.logs[service]
        sync_message = f"SYNC {token}"
        while True:
            line = q.get()
            if line.strip() == sync_message:
                return log_lines
            log_lines.append(line)

    def wait_for_log_message(self, service, regex, timeout_secs=1):
        """Wait for a log message to appear in the logs of a service.

        Poll the log queue of the specified `service` until a log line matches
        the specified `regex`.  Return the log line.  Raise an `Exception` if
        the `timeout_secs` elapses before the log line appears.
        """
        deadline = time.monotonic() + timeout_secs
        q = self.logs[service]
        while True:
            if time.monotonic() > deadline:
                raise Exception(
                    f"Timeout of {timeout_secs} seconds exceeded while waiting for log message matching {regex}."
                )
            try:
                line = q.get_nowait()
                if re.search(regex, line):
                    return line
            except queue.Empty:
                pass

    def find_first_appsec_report(self):
        self.reload_nginx(
        )  # waits for workers to finish; force traces to be sent
        log_lines = self.sync_service("agent")
        entries = [
            json.loads(line) for line in log_lines if line.startswith("[[{")
        ]
        # find _dd.appsec.json in one of the spans of the traces
        for entry in entries:
            for trace in entry:
                for span in trace:
                    if span.get("meta", {}).get("_dd.appsec.json"):
                        return json.loads(span["meta"]["_dd.appsec.json"])
        return None

    def sync_nginx_access_log(self):
        """Send a sync request to nginx and wait until the corresponding access
        log line appears in the output of nginx.  Return the interim log lines
        from nginx.  Raise an `Exception` if an error occurs.
        Note that this assumes that nginx has a particular configuration.
        """
        token = str(uuid.uuid4())
        status, _, body = self.send_nginx_http_request(f"/sync?token={token}")
        if status != 200:
            raise Exception(
                f"nginx returned error (status, body): {(status, body)}")

        log_lines = []
        q = self.logs["nginx"]
        while True:
            line = q.get()
            result = formats.parse_access_log_sync_line(line)
            if result is not None and result["token"] == token:
                return log_lines
            log_lines.append(line)

    def nginx_test_config(self, nginx_conf_text, file_name):
        """Test an nginx configuration.

        Write the specified `nginx_conf_text` to a file in the nginx
        container and tell nginx to check the config as if it were loading it.
        Return `(status, log_lines)`, where `status` is the integer status of
        the nginx check command, and `log_lines` is a chronological list of
        lines from the combined stdout/stderr of the command.
        """
        # Here are some options from `nginx -h`:
        # -t            : test configuration and exit
        # -T            : test configuration, dump it and exit
        # -q            : suppress non-error messages during configuration testing
        # -c filename   : set configuration file (default: /etc/nginx/nginx.conf)
        script = f"""
dir=/tmp
file="$dir/{file_name}"
cat >"$file" <<'END_CONFIG'
error_log stderr notice;
{nginx_conf_text}
END_CONFIG
nginx -t -c "$file"
rcode=$?
exit "$rcode"
"""
        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".
        command = docker_compose_command("exec", "-T", "--", "nginx",
                                         "/bin/sh")
        result = subprocess.run(
            command,
            input=script,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=child_env(),
            encoding="utf8",
        )
        return result.returncode, result.stdout.split("\n")

    def reload_nginx(self, wait_for_workers_to_terminate=True):
        """Send a "reload" signal to nginx.

        If `wait_for_workers_to_terminate` is true, then poll
        `docker compose ps` until the workers associated with nginx's previous
        cycle have terminated.
        """
        with print_duration("Reloading nginx", self.verbose):
            nginx_container = self.containers["nginx"]
            old_worker_pids = None
            if wait_for_workers_to_terminate:
                old_worker_pids = nginx_worker_pids(nginx_container,
                                                    self.verbose)

            # "-T" means "don't allocate a TTY".  This is necessary to avoid
            # the error "the input device is not a TTY".
            command = docker_compose_command(
                "exec",
                "-T",
                "--",
                "nginx",
                "nginx",  # nginx-debug to show debug messages (+ change error_log)
                "-g",
                "pid /run/nginx.pid;",
                "-c",
                nginx_conf_path,
                "-s",
                "reload",
            )
            with print_duration("Sending the reload signal to nginx",
                                self.verbose):
                subprocess.run(
                    command,
                    stdin=subprocess.DEVNULL,
                    stdout=self.verbose,
                    stderr=self.verbose,
                    env=child_env(),
                    check=True,
                )
            if not wait_for_workers_to_terminate:
                return

            # Poll `docker top` until none of `worker_pids` remain.
            # The polling interval was chosen based on a system where:
            # - nginx_worker_pids ran in ~0.05 seconds
            # - the workers terminated after ~6 seconds
            def old_worker_stops(worker_pid):
                _worker_pid = worker_pid

                def check():
                    pids = nginx_worker_pids(nginx_container, self.verbose)
                    res = _worker_pid.intersection(pids)
                    # print(f"_worker_pid={_worker_pid}, pids={pids}, cond={res}")
                    return len(res) == 0

                return check

            wait_until(old_worker_stops(old_worker_pids), timeout_seconds=10)

            def new_worker_starts():
                pids = nginx_worker_pids(nginx_container, self.verbose)
                return len(pids) >= 1

            wait_until(new_worker_starts, timeout_seconds=10)

    def nginx_replace_config(self, nginx_conf_text, file_name):
        """Replace nginx's config and reload nginx.

        Call `self.nginx_test_config(nginx_conf_text, file_name)`.  If the
        resulting status code is zero (success), overwrite nginx's config with
        `nginx_conf_text` and reload nginx.  Return the `(status, log_lines)`
        returned by the call to `nginx_test_config`.
        """
        status, log_lines = self.nginx_test_config(nginx_conf_text, file_name)
        if status:
            return status, log_lines

        script = f"""
>{nginx_conf_path} cat <<'END_CONF'
error_log stderr notice;
{nginx_conf_text}
END_CONF
"""
        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".
        command = docker_compose_command("exec", "-T", "--", "nginx",
                                         "/bin/sh")
        subprocess.run(
            command,
            input=script,
            stdout=self.verbose,
            stderr=self.verbose,
            env=child_env(),
            check=True,
            encoding="utf8",
        )

        self.reload_nginx()
        return status, log_lines

    def nginx_replace_file(self, file, content):
        """Replaces the contents of an arbitrary file in the nginx container."""

        script = f"""
mkdir -p $(dirname "{file}")
>{file} cat <<'END_CONF'
{content}
END_CONF
"""
        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".
        command = docker_compose_command("exec", "-T", "--", "nginx",
                                         "/bin/sh")
        subprocess.run(
            command,
            input=script,
            stdout=self.verbose,
            stderr=self.verbose,
            env=child_env(),
            check=True,
            encoding="utf8",
        )

    @contextlib.contextmanager
    def custom_nginx(self, nginx_conf, extra_env=None, healthcheck_port=None):
        """Yield a managed `Popen` object referring to a new instance of nginx
        running in the nginx service container, where the new instance uses the
        specified `nginx_conf` and has in its environment the optionally
        specified `extra_env`.  When the context of the returned object exits,
        the nginx instance is gracefully shut down.
        Optionally specify an integer `healthcheck_port` at which the
        "/healthcheck" endpoint will be polled in order to determine when the
        nginx instance is ready.
        """
        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".

        # Make a temporary directory.
        command = docker_compose_command("exec", "-T", "--", "nginx", "mktemp",
                                         "-d")
        result = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            encoding="utf8",
            env=child_env(),
            check=True,
        )
        temp_dir = result.stdout.strip()

        # Write the config file to the temporary directory.
        conf_path = temp_dir + "/nginx.conf"
        command = docker_compose_command("exec", "-T", "--", "nginx",
                                         "/bin/sh", "-c",
                                         f"cat >'{conf_path}'")
        subprocess.run(
            command,
            input=nginx_conf,
            stdout=self.verbose,
            stderr=self.verbose,
            encoding="utf8",
            env=child_env(),
            check=True,
        )

        # Start up nginx using the config in the temporary directory and
        # putting the PID file in there too.
        # Let the caller play with the child process, and when they're done,
        # send SIGQUIT to the child process and wait for it to terminate.
        pid_path = temp_dir + "/nginx.pid"
        conf_preamble = f'daemon off; pid "{pid_path}"; error_log stderr notice;'
        env_args = []
        if extra_env is not None:
            for key, value in extra_env.items():
                env_args.append("--env")
                env_args.append(f"{key}={value}")
        command = docker_compose_command(
            "exec",
            "-T",
            *env_args,
            "--",
            "nginx",
            "nginx",
            "-c",
            conf_path,
            "-g",
            conf_preamble,
        )
        child = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=self.verbose,
            stderr=self.verbose,
            env=child_env(),
        )

        # Before we `yield child` to the caller, possibly poll the nginx
        # instance's /healthcheck endpoint to wait for it to be ready.
        if healthcheck_port is not None:
            remaining_attempts = 20
            pause_seconds = 0.25
            before = time.time()
            while remaining_attempts > 0:
                try:
                    status, _, _ = self.send_nginx_http_request(
                        "/healthcheck", port=healthcheck_port)
                    if status == 200:
                        after = time.time()
                        print(
                            f"custom nginx at port {healthcheck_port} took {after - before} seconds to be ready",
                            file=self.verbose,
                        )
                        break
                except Exception:
                    pass
                time.sleep(pause_seconds)
                remaining_attempts -= 1
            if remaining_attempts == 0:
                raise Exception(
                    f"custom nginx at port {healthcheck_port} did not become ready"
                )

        try:
            yield child
        finally:
            # child.terminate() is not enough, not sure why.
            # child.kill() leaves worker processes orphaned,
            # which then screws up `reload_nginx` in subsequent tests.
            # Instead, we signal graceful shutdown by using the `kill`
            # command to send `SIGQUIT`.
            command = docker_compose_command(
                "exec",
                "-T",
                "--",
                "nginx",
                "/bin/sh",
                "-c",
                f"<'{pid_path}' xargs kill -QUIT",
            )
            subprocess.run(
                command,
                stdin=subprocess.DEVNULL,
                stdout=self.verbose,
                stderr=self.verbose,
                encoding="utf8",
                env=child_env(),
                check=True,
            )
            child.wait()

        # Remove the temporary directory.
        command = docker_compose_command("exec", "-T", "--", "nginx", "rm",
                                         "-r", temp_dir)
        subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=self.verbose,
            stderr=self.verbose,
            env=child_env(),
            check=True,
        )


_singleton = LazySingleton(Orchestration, Orchestration.up, Orchestration.down)


def singleton():
    """Return a context manager providing access to the singleton instance of
    `Orchestration`.

    This is meant for use in `with` statements, e.g.

        with orchestration.singleton() as orch:
            status, log_lines = orch.nginx_test_config(...)
    """
    return _singleton.context()
