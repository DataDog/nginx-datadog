"""Service orchestration (docker-compose) facilities for testing"""

from . import formats
from .lazy_singleton import LazySingleton

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


signal.signal(signal.SIGQUIT, quit_signal_handler)

# Since we override the environment variables of child processes,
# `subprocess.Popen` (and its derivatives) need to know exactly where
# the "docker-compose" executable is, since it won't find it in the passed-in
# env's PATH.
docker_compose_command_path = shutil.which('docker-compose')
docker_command_path = shutil.which('docker')

# If we want to always pass some flags to `docker-compose` or to `docker`, put
# them here.  For example, "--tls".  However, TLS behavior can be specified in
# environment variables.
docker_compose_flags = []
docker_flags = []

# Depending on which Docker image nginx is running in, the nginx config might be
# in different locations.  Ideally we could deduce the path to the config file
# by parsing `nginx -V`, but that doesn't always work (e.g. OpenResty).
nginx_conf_path = os.environ.get('NGINX_CONF_PATH', '/etc/nginx/nginx.conf')


def docker_compose_command(*args):
    return [docker_compose_command_path, *docker_compose_flags, *args]


def docker_command(*args):
    return [docker_command_path, *docker_flags, *args]


# docker-compose (at least the version running on my laptop) invokes `docker`
# unqualified, and so when we run `docker-compose` commands, we have to do it
# in an environment where `docker_command_path` is in the PATH.
# See `child_env`.
docker_bin = str(Path(docker_command_path).parent)

# `sync_port` is the port that services will listen on for "sync" requests.
# `sync_port` is the port _inside_ the container -- it will be mapped to an
# ephemeral port on the host.
sync_port = 8888


def child_env(parent_env=None):
    if parent_env is None:
        parent_env = os.environ

    result = {}
    for name in ('BASE_IMAGE', 'NGINX_CONF_PATH', 'NGINX_MODULES_PATH'):
        if name in parent_env:
            result[name] = parent_env[name]

    # Forward DOCKER_HOST, DOCKER_TLS_VERIFY, etc.
    for name, value in parent_env.items():
        if name.startswith('DOCKER_'):
            result[name] = value

    # Forward COMPOSE_PROJECT_NAME (with default), COMPOSE_VERSION, etc.
    result['COMPOSE_PROJECT_NAME'] = parent_env.get('COMPOSE_PROJECT_NAME',
                                                    'test')
    for name, value in parent_env.items():
        if name.startswith('COMPOSE_'):
            result[name] = value

    result['PATH'] = docker_bin

    return result


def to_service_name(container_name):
    # test_foo_bar_1 -> foo_bar
    #
    # Note that the "test_" prefix is the docker-compose project name, which
    # you can override using the COMPOSE_PROJECT_NAME environment
    # variable.  We use that environment variable to hard-code the name to
    # "test".  Otherwise, it defaults to the basename of the directory.  Right
    # now those two are the same, but I don't want the location of these tests
    # to be able to break this.  See mention of COMPOSE_PROJECT_NAME in
    # `child_env()`.
    #
    # When I run docker-compose locally on my machine, the parts of the
    # container name are separated by underscore ("_"), while when I run
    # docker-compose in CircleCI, hyphen ("-") is used.  Go with whichever is
    # being used.
    if '_' in container_name and '-' in container_name:
        raise Exception(
            f"Container name {json.dumps(container_name)} contains both underscores and hyphens.  I can't tell which is being used as a delimiter."
        )

    if '_' in container_name:
        delimiter = '_'
    elif '-' in container_name:
        delimiter = '-'
    else:
        raise Exception(
            f"Container name {json.dumps(container_name)} contains neither underscores nor hyphens.  I don't know which delimiter to use."
        )

    return delimiter.join(container_name.split(delimiter)[1:-1])


def docker_compose_ps(service):
    command = docker_compose_command('ps', '--quiet', service)
    result = subprocess.run(command,
                            stdout=subprocess.PIPE,
                            env=child_env(),
                            encoding='utf8',
                            check=True)
    return result.stdout.strip()


def docker_top(container, verbose_output):
    # `docker top` is picky about the output format of `ps`.  It allows us to
    # pass arbitrary options to `ps`, but, for example, if `pid` is not first,
    # it breaks `docker top`.
    fields = ('pid', 'cmd')

    command = docker_command('top', container, '-o', ','.join(fields))
    with print_duration('Consuming docker-compose top PIDs', verbose_output):
        with subprocess.Popen(command,
                              stdout=subprocess.PIPE,
                              env=child_env(),
                              encoding='utf8') as child:
            # Discard the first line, which is the field names.
            # We could suppress it using ps's `--no-headers` option, but that
            # breaks something inside of `docker top`.
            next(child.stdout)
            # Yield the remaining lines as tuples of fields("cmd" contains spaces,
            # so this is particular to `fields`).
            for line in child.stdout:
                split = line.split()
                pid_str, cmd = split[0], ' '.join(split[1:])
                yield (int(pid_str), cmd)


def nginx_worker_pids(nginx_container, verbose_output):
    # cmd could be "nginx: worker process" or "nginx: worker process shutting down".
    # We want to match both.
    return set(pid for pid, cmd in docker_top(nginx_container, verbose_output)
               if re.match(r'\s*nginx: worker process', cmd))


def with_retries(max_attempts, thunk):
    assert max_attempts > 0
    while True:
        try:
            return thunk()
        except BaseException:
            max_attempts -= 1
            if max_attempts == 0:
                raise


def ready_services():
    """Return the set of names of services that are "ready"."""
    command = docker_compose_command('ps', '--services', '--filter',
                                     'status=running')
    result = subprocess.run(command,
                            stdout=subprocess.PIPE,
                            env=child_env(),
                            encoding='utf8',
                            check=True)
    return set(result.stdout.strip().split('\n'))


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
            os._exit(status_code)  # TODO: doesn't flush IO

    return wrapper


@exit_on_exception
def docker_compose_up(on_ready, logs, verbose_file):
    """This function is meant to be executed on its own thread."""
    containers = {}  # {service: container_id}
    command = docker_compose_command('up', '--remove-orphans',
                                     '--force-recreate', '--no-color')
    before = time.monotonic()
    with subprocess.Popen(command,
                          stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT,
                          env=child_env(),
                          encoding='utf8') as child:
        for line in child.stdout:
            kind, fields = formats.parse_docker_compose_up_line(line)
            print(json.dumps([time.monotonic(), kind, fields]),
                  file=verbose_file,
                  flush=True)

            if kind == 'attach_to_logs':
                # Done starting containers.  Time to deliver the container IDs
                # to our caller.
                after = time.monotonic()
                print(
                    f'It took {after - before} seconds to create all service containers.',
                    file=verbose_file,
                    flush=True)
                with print_duration('Waiting for services to be ready',
                                    verbose_file):
                    while len(ready_services()) != len(containers):
                        poll_seconds = 0.1
                        print('Not all services are ready.  Going to wait',
                              poll_seconds,
                              'seconds',
                              file=verbose_file)
                        time.sleep(poll_seconds)
                on_ready({'containers': containers})
            elif kind == 'finish_create_container':
                # Started a container.  Add its container ID to `containers`.
                container_name = fields['container']
                service = to_service_name(container_name)
                # Consult `docker-compose ps` for the service's container ID.
                # Since we're handling a finish_create_container event, you'd
                # think that the container would be available now.  However,
                # with CircleCI's remote docker setup, there's a race here
                # where docker-compose does not yet know which container
                # corresponds to `service` (even though it just told us that
                # the container was created).  So, we retry a few times.
                containers[service] = with_retries(
                    5, lambda: docker_compose_ps(service))
            elif kind == 'service_log':
                # Got a line of logging from some service.  Push it onto the
                # appropriate queue for consumption by tests.
                service = fields['service']
                payload = fields['payload']
                logs[service].put(payload)


@contextlib.contextmanager
def print_duration(of_what, output):
    before = time.monotonic()
    yield
    after = time.monotonic()
    print(f'{of_what} took {after - before:.5} seconds.',
          file=output,
          flush=True)


def docker_compose_services():
    command = docker_compose_command('config', '--services')
    result = subprocess.run(command,
                            stdout=subprocess.PIPE,
                            env=child_env(),
                            encoding='utf8',
                            check=True)
    return result.stdout.split()


def curl(url, headers, stderr=None):

    def header_args():
        for name, value in headers.items():
            yield '--header'
            yield f'{name}: {value}'

    # "curljson.sh" is a script that lives in the "client" docker-compose
    # service.  It's a wrapper around "curl" that outputs a JSON object of
    # information on the first line, and outputs a JSON string of the response
    # body on the second line.  See the documentation of the "json" format for
    # the "--write-out" option in curl's manual for more information on the
    # properties of the JSON object.

    # "-T" means "don't allocate a TTY".  This prevents `jq` from outputting in
    # color.
    command = docker_compose_command('exec', '-T', '--', 'client',
                                     'curljson.sh', *header_args(), url)
    result = subprocess.run(command,
                            stdout=subprocess.PIPE,
                            stderr=stderr,
                            env=child_env(),
                            encoding='utf8',
                            check=True)
    fields_json, body_json, *rest = result.stdout.split('\n')
    if any(line for line in rest):
        raise Exception('Unexpected trailing output to curljson.sh: ' +
                        json.dumps(rest))

    fields = json.loads(fields_json)
    body = json.loads(body_json)
    return fields, body


class Orchestration:
    """A handle for a `docker-compose` session.

    `up()` runs `docker-compose up` and spawns a thread that consumes its
    output.

    `down()` runs `docker-compose down`.

    Other methods perform integration test specific actions.

    This class is meant to be accessed through the `singleton()` function of
    this module, not instantiated directly.
    """

    def __init__(self):
        print('The test runner is running in the following environment:',
              os.environ)

    # Properties (all private)
    # - `up_thread` is the `threading.Thread` running `docker-compose up`.
    # - `logs` is a `dict` that maps service name to a `queue.SimpleQueue` of log
    #   lines.
    # - `containers` is a `dict` {service: container ID} that, per service,
    #   maps to the Docker container ID in which the service is running.
    # - `services` is a `list` of service names as defined in the
    #   `docker-compose` config.
    # - `verbose` is a file-like object to which verbose logging is written.

    def up(self):
        """Start service orchestration.

        Run `docker-compose up` to bring up the orchestrated services.  Begin
        parsing their logs on a separate thread.
        """
        project_name = child_env()['COMPOSE_PROJECT_NAME']
        self.verbose = (Path(__file__).parent.resolve().parent /
                        f'logs/{project_name}.log').open('w')

        # Before we bring things up, first clean up any detritus left over from
        # previous runs.  Failing to do so can create problems later when we
        # ask docker-compose which container a service is running in.
        command = docker_compose_command('down', '--remove-orphans')
        subprocess.run(command,
                       stdin=subprocess.DEVNULL,
                       stdout=self.verbose,
                       stderr=self.verbose,
                       env=child_env(),
                       check=True)

        self.services = docker_compose_services()
        print('services:', self.services, file=self.verbose, flush=True)
        self.logs = {service: queue.SimpleQueue() for service in self.services}
        ready_queue = queue.SimpleQueue()
        self.up_thread = threading.Thread(target=docker_compose_up,
                                          args=(ready_queue.put, self.logs,
                                                self.verbose))
        self.up_thread.start()
        runtime_info = ready_queue.get()
        self.containers = runtime_info['containers']
        print(runtime_info, file=self.verbose, flush=True)

    def down(self):
        """Stop service orchestration.

        Run `docker-compose down` to bring down the orchestrated services.
        Join the log-parsing thread.
        """
        command = docker_compose_command('down', '--remove-orphans')
        with print_duration('Bringing down all services', self.verbose):
            with subprocess.Popen(
                    command,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE,  # "Stopping test_foo_1   ... done"
                    env=child_env(),
                    encoding='utf8') as child:
                for line in child.stderr:
                    kind, fields = formats.parse_docker_compose_down_line(line)
                    print(json.dumps((kind, fields)),
                          file=self.verbose,
                          flush=True)

            self.up_thread.join()

        self.verbose.close()

    def send_nginx_http_request(self, path, port=80, headers={}):
        """Send a "GET <path>" request to nginx, and return the resulting HTTP
        status code and response body as a tuple `(status, body)`.
        """
        url = f'http://nginx:{port}{path}'
        print('fetching', url, file=self.verbose, flush=True)
        fields, body = curl(url, headers, stderr=self.verbose)
        return (fields['response_code'], body)

    def send_nginx_grpc_request(self, symbol, port=1337):
        """Send an empty gRPC request to the nginx endpoint at "/", where
        the gRPC request is named by `symbol`, which has the form
        "package.service.request".  The request is made using the `grpcurl`
        command line utility.  Return the exit status of `grpcurl` and its
        combined stdout and stderr as a tuple `(status, output)`.
        """
        address = f'nginx:{port}'
        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".
        command = docker_compose_command('exec', '-T', '--', 'client',
                                         'grpcurl', '-plaintext', address,
                                         symbol)
        result = subprocess.run(command,
                                stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,
                                encoding='utf8',
                                env=child_env())
        return result.returncode, result.stdout

    def sync_service(self, service):
        """Establish synchronization points in the logs of a service.

        Send a "sync" request to the specified `service`,
        and wait for the corresponding log messages to appear in the
        docker-compose log.  This way, we know that whatever we have done
        previously has already appeared in the log.

            log_lines = orch.sync_service('agent')

        where `log_lines` is a chronological list of strings, each a line from
        the service's log.
        """
        token = str(uuid.uuid4())
        fields, body = curl(f'http://{service}:{sync_port}',
                            headers={'X-Datadog-Test-Sync-Token': token})

        assert fields['response_code'] == 200

        log_lines = []
        q = self.logs[service]
        sync_message = f'SYNC {token}'
        while True:
            line = q.get()
            if line.strip() == sync_message:
                return log_lines
            log_lines.append(line)

    def sync_nginx_access_log(self):
        """Send a sync request to ngnix and wait until the corresponding access
        log line appears in the output of nginx.  Return the interim log lines
        from nginx.  Raise an `Exception` if an error occurs.
        Note that this assumes that nginx has a particular configuration.
        """
        token = str(uuid.uuid4())
        status, body = self.send_nginx_http_request(f'/sync?token={token}')
        if status != 200:
            raise Exception(
                f'nginx returned error (status, body): {(status, body)}')

        log_lines = []
        q = self.logs['nginx']
        while True:
            line = q.get()
            result = formats.parse_access_log_sync_line(line)
            if result is not None and result['token'] == token:
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
dir=$(mktemp -d)
file="$dir/{file_name}"
cat >"$file" <<'END_CONFIG'
{nginx_conf_text}
END_CONFIG
nginx -t -c "$file"
rcode=$?
rm -r "$dir"
exit "$rcode"
"""
        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".
        command = docker_compose_command('exec', '-T', '--', 'nginx',
                                         '/bin/sh')
        result = subprocess.run(command,
                                input=script,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,
                                env=child_env(),
                                encoding='utf8')
        return result.returncode, result.stdout.split('\n')

    def reload_nginx(self, wait_for_workers_to_terminate=True):
        """Send a "reload" signal to nginx.

        If `wait_for_workers_to_terminate` is true, then poll
        `docker-compose ps` until the workers associated with nginx's previous
        cycle have terminated.
        """
        with print_duration('Reloading nginx', self.verbose):
            nginx_container = self.containers['nginx']
            old_worker_pids = None
            if wait_for_workers_to_terminate:
                old_worker_pids = nginx_worker_pids(nginx_container,
                                                    self.verbose)

            # "-T" means "don't allocate a TTY".  This is necessary to avoid
            # the error "the input device is not a TTY".
            command = docker_compose_command('exec', '-T', '--', 'nginx',
                                             'nginx', '-s', 'reload')
            with print_duration('Sending the reload signal to nginx',
                                self.verbose):
                subprocess.run(command,
                               stdin=subprocess.DEVNULL,
                               stdout=self.verbose,
                               stderr=self.verbose,
                               env=child_env(),
                               check=True)
            if not wait_for_workers_to_terminate:
                return

            # Poll `docker top` until none of `worker_pids` remain.
            # The polling interval was chosen based on a system where:
            # - nginx_worker_pids ran in ~0.05 seconds
            # - the workers terminated after ~6 seconds
            poll_period_seconds = 0.5
            # TODO timeout_seconds = 10
            timeout_seconds = 9999999999999999999999999
            before = time.monotonic()
            while old_worker_pids & nginx_worker_pids(nginx_container,
                                                      self.verbose):
                now = time.monotonic()
                if now - before >= timeout_seconds:
                    raise Exception(
                        f'{timeout_seconds} seconds timeout exceeded while waiting for nginx workers to stop.  {now - before} seconds elapsed.'
                    )
                time.sleep(poll_period_seconds)

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
{nginx_conf_text}
END_CONF
"""
        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".
        command = docker_compose_command('exec', '-T', '--', 'nginx',
                                         '/bin/sh')
        subprocess.run(command,
                       input=script,
                       stdout=self.verbose,
                       stderr=self.verbose,
                       env=child_env(),
                       check=True,
                       encoding='utf8')

        self.reload_nginx()
        return status, log_lines

    @contextlib.contextmanager
    def custom_nginx(self, nginx_conf, extra_env=None):
        """Yield a managed `Popen` object referring to a new instance of nginx
        running in the nginx service container, where the new instance uses the
        specified `nginx_conf` and has in its environment the optionally
        specified `extra_env`.  When the context of the returned object exits,
        the nginx instance is gracefully shut down.
        """
        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".

        # Make a temporary directory.
        command = docker_compose_command('exec', '-T', '--', 'nginx', 'mktemp',
                                         '-d')
        result = subprocess.run(command,
                                stdin=subprocess.DEVNULL,
                                capture_output=True,
                                encoding='utf8',
                                env=child_env(),
                                check=True)
        temp_dir = result.stdout.strip()

        # Write the config file to the temporary directory.
        conf_path = temp_dir + '/nginx.conf'
        command = docker_compose_command('exec', '-T', '--', 'nginx',
                                         '/bin/sh', '-c',
                                         f"cat >'{conf_path}'")
        subprocess.run(command,
                       input=nginx_conf,
                       stdout=self.verbose,
                       stderr=self.verbose,
                       encoding='utf8',
                       env=child_env(),
                       check=True)

        # Start up nginx using the config in the temporary directory and
        # putting the PID file in there too.
        # Let the caller play with the child process, and when they're done,
        # send SIGQUIT to the child process and wait for it to terminate.
        pid_path = temp_dir + '/nginx.pid'
        conf_preamble = f'daemon off; pid "{pid_path}"; error_log stderr;'
        env_args = []
        if extra_env is not None:
            for key, value in extra_env.items():
                env_args.append('--env')
                env_args.append(f'{key}={value}')
        command = docker_compose_command('exec', '-T', *env_args, '--',
                                         'nginx', 'nginx', '-c', conf_path,
                                         '-g', conf_preamble)
        child = subprocess.Popen(command,
                                 stdin=subprocess.DEVNULL,
                                 stdout=self.verbose,
                                 stderr=self.verbose,
                                 env=child_env())
        try:
            yield child
        finally:
            # child.terminate() is not enough, not sure why.
            # child.kill() leaves worker processes orphaned,
            # which then screws up `reload_nginx` in subsequent tests.
            # Instead, we signal graceful shutdown by using the `kill`
            # command to send `SIGQUIT`.
            command = docker_compose_command(
                'exec', '-T', '--', 'nginx', '/bin/sh', '-c',
                f"<'{pid_path}' xargs kill -QUIT")
            subprocess.run(command,
                           stdin=subprocess.DEVNULL,
                           stdout=self.verbose,
                           stderr=self.verbose,
                           encoding='utf8',
                           env=child_env(),
                           check=True)
            child.wait()

        # Remove the temporary directory.
        command = docker_compose_command('exec', '-T', '--', 'nginx', 'rm',
                                         '-r', temp_dir)
        subprocess.run(command,
                       stdin=subprocess.DEVNULL,
                       stdout=self.verbose,
                       stderr=self.verbose,
                       env=child_env(),
                       check=True)


_singleton = LazySingleton(Orchestration, Orchestration.up, Orchestration.down)


def singleton():
    """Return a context manager providing access to the singleton instance of
    `Orchestration`.

    This is meant for use in `with` statements, e.g.

        with orchestration.singleton() as orch:
            status, log_lines = orch.nginx_test_config(...)
    """
    return _singleton.context()
