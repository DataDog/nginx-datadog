#!/usr/bin/env python3

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


# Since we override the environment variables of child processes,
# `subprocess.Popen` (and its derivatives) need to know exactly where
# the "docker-compose" executable is, since it won't find it in the passed-in
# env's PATH.
DOCKER_COMPOSE = shutil.which('docker-compose')


def try_match(pattern, subject):
    return re.fullmatch(pattern, subject, re.MULTILINE | re.DOTALL)


def to_service_name(container_name):
    # test_foo_bar_1 -> foo_bar
    #
    # Note that the "test_" prefix is the docker-compose project name, which
    # you can override using the COMPOSE_PROJECT_NAME environment
    # variable.  We use that environment variable to hard-code the name to
    # "test".  Otherwise, it defaults to the basename of the directory.  Right
    # now those two are the same, but I don't want the location of these tests
    # to be able to break this.
    return '_'.join(container_name.split('_')[1:-1])


def child_env(parent_env=None):
    if parent_env is None:
        parent_env = os.environ
    
    result = {}
    if 'NGINX_IMAGE' in parent_env:
        result['NGINX_IMAGE'] = parent_env['NGINX_IMAGE']
    else:
        # [repo]/test/bin/this_file.py  ->  [repo]/nginx-version
        version = (Path(__file__).resolve().parent.parent.parent/'nginx-version').read_text()
        result['NGINX_IMAGE'] = f'nginx:{version}'
        
    if 'DOCKER_HOST' in parent_env:
        result['DOCKER_HOST'] = parent_env['DOCKER_HOST']
        
    result['COMPOSE_PROJECT_NAME'] = parent_env.get('COMPOSE_PROJECT_NAME', 'test')
    
    return result


def parse_line(line):
    # service_log(service)
    match = try_match(r'(?P<service>\S+)_\d+\s*\| (?P<payload>.*)\n', line)
    if match is not None:
        return ('service_log', match.groupdict()['service'], match.groupdict()['payload'])

    # begin_create_container(container)
    begin_create_container = r'Creating (?P<container>\S+)\s*\.\.\.\s*'
    match = try_match(begin_create_container, line)
    if match is not None:
        return ('begin_create_container', match.groupdict()['container'])

    # finish_create_container(container)
    match = try_match(begin_create_container + r'done\s*', line)
    if match is not None:
        return ('finish_create_container', match.groupdict()['container'])

    # attach_to_logs([container, ...])
    match = try_match(r'Attaching to (?P<containers>\S+(, \S+)*\s*)', line)
    if match is not None:
        return ('attach_to_logs', match.groupdict()['containers'].split(', '))
    
    # image_build_success(image)
    match = try_match(r'Successfully built (?P<image>\S+)\s*', line)
    if match is not None:
        return ('image_build_success', match.groupdict()['image'])

    return ('other', line)


if __name__ == '__main__':
    args = [DOCKER_COMPOSE, 'up', '--build', '--remove-orphans', '--no-color']
    ports = {}
    try:
        with subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=child_env(), encoding='utf8') as child:
            for i, line in enumerate(child.stdout):
                parsed = parse_line(line)
                print(i, parsed)
                what, *rest = parsed
                if what == 'finish_create_container':
                    container, = rest
                    # test_foo_bar_1 -> foo_bar
                    service = to_service_name(container)
                    # We're interesting in the "admin" interfaces of the
                    # proxied services -- port 8888 by convention.
                    # Nginx, on the other hand, listens on port 80.
                    inside_ports = [80] if service == 'nginx' else [8888]
                    for port in inside_ports:
                        result = subprocess.run([DOCKER_COMPOSE, 'port', service, str(port)], capture_output=True, encoding='utf8', env=child_env())
                        print('output of docker-compose port: ', result.stdout)
                        _, ephemeral = result.stdout.strip().split(':')
                        ephemeral = int(ephemeral)
                        ports.setdefault(service, {})[port] = ephemeral
                    print('port mappings so far:', ports)

    except KeyboardInterrupt:
        subprocess.run([DOCKER_COMPOSE, 'down', '--remove-orphans'], encoding='utf8', env=child_env())

    print('exiting ...')
    sys.exit(child.returncode)
