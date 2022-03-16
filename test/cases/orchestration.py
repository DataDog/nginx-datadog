"""Service orchestration (docker-compose) facilities for testing"""

import formats
from lazy_singleton import LazySingleton

import contextlib
import json
import os
from pathlib import Path
import queue
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
docker_compose_exe = shutil.which('docker-compose')

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
    command = [docker_compose_exe, 'port', service, str(inside_port)]
    ip, port = subprocess.run(command,
                              stdout=subprocess.PIPE,
                              env=child_env(),
                              encoding='utf8',
                              check=True).stdout.split(':')
    return ip, int(port)


def docker_compose_up(deliver_ports, logs):
    """This function is meant to be executed on its own thread."""
    ports = {}  # {service: {inside: outside}}
    command = [
        docker_compose_exe, 'up', '--build', '--remove-orphans',
        '--force-recreate', '--no-color'
    ]
    before = time.monotonic()
    with subprocess.Popen(command,
                          stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT,
                          env=child_env(),
                          encoding='utf8') as child, open(
                              'docker-compose-up.log', 'a') as verbose:
        for line in child.stdout:
            kind, fields = formats.parse_docker_compose_up_line(line)
            # TODO: Maybe suppress this
            print(time.monotonic(),
                  json.dumps([kind, fields]),
                  file=verbose,
                  flush=True)

            if kind == 'attach_to_logs':
                # Done starting containers.  Time to deliver the ephemeral port
                # mappings to our caller.
                after = time.monotonic()
                print(
                    f'It took {after - before} seconds to start all services.')
                deliver_ports(ports)
            elif kind == 'finish_create_container':
                # Started a container.  Add its ephemeral port mappings to
                # `ports`.
                container_name = fields['container']
                service = to_service_name(container_name)
                # For nginx we're interested in its HTTP and gRPC ports.
                # For everything else, we're interested in the "sync" port (sync_port).
                inside_ports = [80, 8080
                                ] if service == 'nginx' else [sync_port]
                for inside_port in inside_ports:
                    _, outside_port = docker_compose_port(service, inside_port)
                    ports.setdefault(service, {})[inside_port] = outside_port
            elif kind == 'service_log':
                # Got a line of logging from some service.  Push it onto the
                # appropriate queue for consumption by tests.
                service = fields['service']
                payload = fields['payload']
                logs[service].put(payload)


def docker_compose_services():
    command = [docker_compose_exe, 'config', '--services']
    result = subprocess.run(command,
                            stdout=subprocess.PIPE,
                            env=child_env(),
                            encoding='utf8',
                            check=True)
    return result.stdout.split()


class Orchestration:
    """TODO"""

    # Properties (all private)
    # - `up_thread` is the `threading.Thread` running `docker-compose up`.
    # - `logs` is a `dict` that maps service name to a `queue.SimpleQueue` of log
    #   lines.
    # - `ports` is a `dict` {service: {inside: outside}} that, per service,
    #   maps port bindings from the port inside the service to the port outside
    #   (which will be an ephemeral port chosen by the system at runtime).
    # - `services` is a `list` of service names as defined in the
    #   `docker-compose` config.

    def up(self):
        """Start service orchestration.

        Run `docker-compose up` to bring up the orchestrated services.  Begin
        parsing their logs on a separate thread.
        """
        self.services = docker_compose_services()
        print('services:', self.services)  # TODO: no
        self.logs = {service: queue.SimpleQueue() for service in self.services}
        ports_queue = queue.SimpleQueue()
        self.up_thread = threading.Thread(target=docker_compose_up,
                                          args=(ports_queue.put, self.logs))
        self.up_thread.start()
        self.ports = ports_queue.get()
        print('ports:', self.ports)  # TODO: no

    def down(self):
        """Stop service orchestration.
        
        Run `docker-compose down` to bring down the orchestrated services.
        Join the log-parsing thread.
        """
        command = [docker_compose_exe, 'down', '--remove-orphans']
        before = time.monotonic()
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
                print(json.dumps((kind, fields)))
                if kind == 'remove_network':
                    continue
                times.setdefault(fields['container'],
                                 {})[kind] = time.monotonic()

        self.up_thread.join()
        after = time.monotonic()
        print(f'It took {after - before} seconds to bring down all services.')
        for container, timestamps in times.items():
            begin_stop, end_stop = timestamps[
                'begin_stop_container'], timestamps['end_stop_container']
            print(f'{container} took {end_stop - begin_stop} seconds to stop.')
            # Container removal happens quickly.  It's the stopping that's taking too long.
            # begin_remove, end_remove = timestamps['begin_remove_container'], timestamps['end_remove_container']
            # print(f'{container} took {end_remove - begin_remove} seconds to remove.')

    # TODO: not like this. Just playing with it.
    def send_nginx_request(self, path):
        """Send a "GET <path>" request to nginx, and return the resulting HTTP
        status code.
        """
        outside_port = self.ports['nginx'][80]
        return urllib.request.urlopen(
            f'http://localhost:{outside_port}{path}').status

    def sync_proxied_service(self, service):
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
            line = q.get()
            if line.strip() == sync_message:
                return log_lines
            log_lines.append(line)

    def sync_proxied_services(self):
        """Establish synchronization points in the logs of proxied services.
        
        Send a "sync" request to each of the services reverse proxied by
        nginx, and wait for the corresponding log messages to appear in the
        docker-compose log.  This way, we know that whatever we have done
        previously has already appeared in the log.
        
            logs_by_service = orch.sync_proxied_services()

        where `logs_by_service` is a `dict` mapping service name to a
        chronological list of log lines gathered since the previous sync.
        """
        # TODO

    def nginx_test_config(self, nginx_conf_text):
        """Test an nginx configuration.

        Write the specified `nginx_conf_text` to a file in the nginx
        container and tell nginx to check the config as if it were loading it.
        Return `(status, log_lines)`, where `status` is the integer status of
        the nginx check command, and `log_lines` is a chronological list of
        lines from the ouptut of the command.
        """
        # TODO

    def nginx_reset(self):
        """Restore nginx to a default configuration.

        Overwrite nginx's config with a default, send it a "/sync" request,
        and wait for the corresponding log message to appear in the access
        log.
        """
        # TODO

    def nginx_replace_config(self, nginx_conf_text):
        """Replace nginx's config and reload nginx.

        Call `self.nginx_test_config(nginx_conf_text)`.  If the resulting
        status code is zero (success), overwrite nginx's config with
        `nginx_conf_text` and reload nginx.  Return the `(status, log_lines)`
        returned by the call to `nginx_test_config`.
        """
        # TODO


_singleton = LazySingleton(Orchestration, Orchestration.up, Orchestration.down)


def singleton():
    """Return a context manager providing access to the singleton instance of
    `Orchestration`.

    This is meant for use in `with` statements, e.g.

        with orchestration.singleton() as orch:
            status, log_lines = orch.nginx_test_config(...)
    """
    return _singleton.context()
