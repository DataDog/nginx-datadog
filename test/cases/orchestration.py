"""Service orchestration (docker-compose) facilities for testing"""

from . import formats
from .lazy_singleton import LazySingleton

import contextlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import threading
import time
import urllib.request
import uuid

# Since we override the environment variables of child processes,
# `subprocess.Popen` (and its derivatives) need to know exactly where
# the "docker-compose" executable is, since it won't find it in the passed-in
# env's PATH.
docker_compose_command = shutil.which('docker-compose')
docker_exe = shutil.which('docker')

# docker-compose (at least the version running on my laptop) invokes
# `docker` unqualified, and so when we run `docker-compose` commands,
# we have to do it in an environment where `docker_exe` is in the PATH.
# See `child_env`.
docker_bin = str(Path(docker_exe).parent)

# `sync_port` is the port that services will listen on for "sync" requests.
# `sync_port` is the port _inside_ the container -- it will be mapped to an
# ephemeral port on the host.
sync_port = 8888


def child_env(parent_env=None):
    if parent_env is None:
        parent_env = os.environ

    result = {}
    if 'NGINX_IMAGE' in parent_env:
        result['NGINX_IMAGE'] = parent_env['NGINX_IMAGE']
    else:
        # [repo]/test/cases/orchestration.py  ->  [repo]/nginx-version
        # TODO: Should this be injected into the tests instead? (e.g. using an environment variable)
        version = (Path(__file__).resolve().parent.parent.parent /
                   'nginx-version').read_text()
        result['NGINX_IMAGE'] = f'nginx:{version}'

    if 'DOCKER_HOST' in parent_env:
        result['DOCKER_HOST'] = parent_env['DOCKER_HOST']

    result['COMPOSE_PROJECT_NAME'] = parent_env.get('COMPOSE_PROJECT_NAME',
                                                    'test')

    # TODO: combine with parent PATH?
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
    return '_'.join(container_name.split('_')[1:-1])


def docker_compose_port(service, inside_port):
    command = [docker_compose_command, 'port', service, str(inside_port)]
    ip, port = subprocess.run(command,
                              stdout=subprocess.PIPE,
                              env=child_env(),
                              encoding='utf8',
                              check=True).stdout.split(':')
    return ip, int(port)


def docker_compose_ps(service):
    command = [docker_compose_command, 'ps', '--quiet', service]
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

    command = [docker_exe, 'top', container, '-o', ','.join(fields)]
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


def docker_compose_up(on_ready, logs, verbose_file):
    """This function is meant to be executed on its own thread."""
    ports = {}  # {service: {inside: outside}}
    containers = {}  # {service: container_id}
    command = [
        docker_compose_command, 'up', '--build', '--remove-orphans',
        '--force-recreate', '--no-color'
    ]
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
                # Done starting containers.  Time to deliver the ephemeral port
                # mappings and container IDs to our caller.
                after = time.monotonic()
                print(
                    f'It took {after - before} seconds to start all services.',
                    file=verbose_file,
                    flush=True)
                on_ready({'ports': ports, 'containers': containers})
            elif kind == 'finish_create_container':
                # Started a container.  Add its ephemeral port mappings to
                # `ports` and its container ID to `containers`.
                container_name = fields['container']
                service = to_service_name(container_name)
                # For nginx we're interested in its HTTP and gRPC ports.
                # For everything else, we're interested in the "sync" port (sync_port).
                inside_ports = [80, 1337, 8080
                                ] if service == 'nginx' else [sync_port]
                for inside_port in inside_ports:
                    _, outside_port = docker_compose_port(service, inside_port)
                    ports.setdefault(service, {})[inside_port] = outside_port
                # Consult `docker-compose ps` for the service's container ID
                containers[service] = docker_compose_ps(service)
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
    print(f'{of_what} took {after - before} seconds.', file=output)


def docker_compose_services():
    command = [docker_compose_command, 'config', '--services']
    result = subprocess.run(command,
                            stdout=subprocess.PIPE,
                            env=child_env(),
                            encoding='utf8',
                            check=True)
    return result.stdout.split()


class Orchestration:
    """A handle for a `docker-compose` session.
    
    `up()` runs `docker-compose up` and spawns a thread that consumes its
    output.

    `down()` runs `docker-compose down`.

    Other methods perform integration test specific actions.

    This class is meant to be accessed through the `singleton()` function of
    this module, not instantiated directly.
    """

    # Properties (all private)
    # - `up_thread` is the `threading.Thread` running `docker-compose up`.
    # - `logs` is a `dict` that maps service name to a `queue.SimpleQueue` of log
    #   lines.
    # - `ports` is a `dict` {service: {inside: outside}} that, per service,
    #   maps port bindings from the port inside the service to the port outside
    #   (which will be an ephemeral port chosen by the system at runtime).
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
        # Before we bring things up, first clean up any detritus left over from
        # previous runs.  Failing to do so can create problems later when we
        # ask docker-compose which container a service is running in.
        command = [docker_compose_command, 'down', '--remove-orphans']
        subprocess.run(command,
                       stdin=subprocess.DEVNULL,
                       stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL,
                       env=child_env(),
                       check=True)

        self.verbose = (Path(__file__).parent.resolve().parent /
                        'docker-compose-verbose.log').open('a')

        self.services = docker_compose_services()
        print('services:', self.services, file=self.verbose, flush=True)
        self.logs = {service: queue.SimpleQueue() for service in self.services}
        ready_queue = queue.SimpleQueue()
        self.up_thread = threading.Thread(target=docker_compose_up,
                                          args=(ready_queue.put, self.logs,
                                                self.verbose))
        self.up_thread.start()
        runtime_info = ready_queue.get()
        self.ports = runtime_info['ports']
        self.containers = runtime_info['containers']
        print(runtime_info, file=self.verbose, flush=True)

    def down(self):
        """Stop service orchestration.
        
        Run `docker-compose down` to bring down the orchestrated services.
        Join the log-parsing thread.
        """
        command = [docker_compose_command, 'down', '--remove-orphans']
        with print_duration('Bringing down all services', self.verbose):
            with subprocess.Popen(
                    command,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE,  # "Stopping test_foo_1   ... done"
                    env=child_env(),
                    encoding='utf8') as child:
                times = {}
                for line in child.stderr:
                    kind, fields = formats.parse_docker_compose_down_line(line)
                    print(json.dumps((kind, fields)),
                          file=self.verbose,
                          flush=True)
                    if kind == 'remove_network':
                        continue
                    times.setdefault(fields['container'],
                                     {})[kind] = time.monotonic()

            self.up_thread.join()

        for container, timestamps in times.items():
            begin_stop, end_stop = timestamps[
                'begin_stop_container'], timestamps['end_stop_container']
            print(f'{container} took {end_stop - begin_stop} seconds to stop.',
                  file=self.verbose)
            # Container removal happens quickly.  It's the stopping that was
            # taking too long before I fixed Node's SIGTERM handler.
            # begin_remove, end_remove = timestamps['begin_remove_container'], timestamps['end_remove_container']
            # print(f'{container} took {end_remove - begin_remove} seconds to remove.', file=self.verbose)
        self.verbose.close()

    # TODO: not like this. Just playing with it.
    def send_nginx_request(self, path, inside_port=80):
        """Send a "GET <path>" request to nginx, and return the resulting HTTP
        status code and response body as a tuple `(status, body)`.
        """
        outside_port = self.ports['nginx'][inside_port]
        url = f'http://localhost:{outside_port}{path}'
        print('fetching', url, file=self.verbose, flush=True)
        response = urllib.request.urlopen(url)
        return (response.status, response.read())

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
        outside_port = self.ports[service][sync_port]
        token = str(uuid.uuid4())
        request = urllib.request.Request(
            f'http://localhost:{outside_port}',
            headers={'X-Datadog-Test-Sync-Token': token})

        result = urllib.request.urlopen(request)
        assert result.status == 200  # TODO

        log_lines = []
        q = self.logs[service]
        sync_message = f'SYNC {token}'
        while True:
            print(
                f'There are {q.qsize()} log lines waiting in the queue for service {service}'
            )  # TODO: no
            line = q.get()
            print(f'Dequeued a log line service {service}: {repr(line)}'
                  )  # TODO: no
            if line.strip() == sync_message:
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
        command = [
            docker_compose_command, 'exec', '-T', '--', 'nginx', '/bin/sh'
        ]
        result = subprocess.run(command,
                                input=script,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,
                                env=child_env(),
                                encoding='utf8')
        return result.returncode, result.stdout.split('\n')

    def nginx_reset(self):
        """Restore nginx to a default configuration.

        Overwrite nginx's config with a default, send it a "reload" signal,
        and wait for the old worker processes to terminate.
        """
        # TODO

    def reload_nginx(self, wait_for_workers_to_terminate=False):
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
                print('old worker PIDs:', old_worker_pids)

            # "-T" means "don't allocate a TTY".  This is necessary to avoid
            # the error "the input device is not a TTY".
            command = [
                docker_compose_command, 'exec', '-T', '--', 'nginx', 'nginx',
                '-s', 'reload'
            ]
            with print_duration('Sending the reload signal to nginx',
                                self.verbose):
                subprocess.run(command,
                               stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL,
                               env=child_env(),
                               check=True)
            if not wait_for_workers_to_terminate:
                return

            # Poll `docker top` until none of `worker_pids` remain.
            # The polling interval was chosen based on a system where:
            # - nginx_worker_pids ran in ~0.05 seconds
            # - the workers terminated after ~6 seconds
            poll_period_seconds = 0.5
            while old_worker_pids & nginx_worker_pids(nginx_container,
                                                      self.verbose):
                time.sleep(poll_period_seconds)

    def nginx_replace_config(self, nginx_conf_text):
        """Replace nginx's config and reload nginx.

        Call `self.nginx_test_config(nginx_conf_text)`.  If the resulting
        status code is zero (success), overwrite nginx's config with
        `nginx_conf_text` and reload nginx.  Return the `(status, log_lines)`
        returned by the call to `nginx_test_config`.
        """
        # TODO

    @contextlib.contextmanager
    def custom_nginx(self, nginx_conf, extra_env=None):
        """TODO
        """
        # TODO: This function doesn't actually use `self`.

        # "-T" means "don't allocate a TTY".  This is necessary to avoid the
        # error "the input device is not a TTY".

        # Make a temporary directory.
        command = [
            docker_compose_command, 'exec', '-T', '--', 'nginx', 'mktemp', '-d'
        ]
        result = subprocess.run(command,
                                stdin=subprocess.DEVNULL,
                                capture_output=True,
                                encoding='utf8',
                                env=child_env(),
                                check=True)
        temp_dir = result.stdout.strip()

        # Write the config file to the temporary directory.
        conf_path = temp_dir + '/nginx.conf'
        command = [
            docker_compose_command, 'exec', '-T', '--', 'nginx', '/bin/sh',
            '-c', f"cat >'{conf_path}'"
        ]
        subprocess.run(command,
                       input=nginx_conf,
                       stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL,
                       encoding='utf8',
                       env=child_env(),
                       check=True)

        # Start up nginx using the config in the temporary directory and
        # putting the PID file in there too.
        # Let the caller play with the child process, and when they're done,
        # send SIGKILL (SIGTERM doesn't do it) to the child process and wait
        # for it to terminate.
        pid_path = temp_dir + '/nginx.pid'
        conf_preamble = f'daemon off; pid "{pid_path}"; error_log stderr;'
        env_args = []
        if extra_env is not None:
            for key, value in extra_env.items():
                env_args.append('--env')
                env_args.append(f'{key}={value}')
        command = [
            docker_compose_command, 'exec', '-T', *env_args, '--', 'nginx',
            'nginx', '-c', conf_path, '-g', conf_preamble
        ]
        env = child_env() | extra_env
        # TODO: It would be good to get output for logging on error, but how?
        child = subprocess.Popen(command,
                                 stdin=subprocess.DEVNULL,
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL,
                                 env=env)
        try:
            yield child
        finally:
            # child.terminate() is not enough, not sure why
            child.kill()
            child.wait()

        # Remove the temporary directory.
        command = [
            docker_compose_command, 'exec', '-T', '--', 'nginx', 'rm', '-r',
            temp_dir
        ]
        subprocess.run(command,
                       stdin=subprocess.DEVNULL,
                       stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL,
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
