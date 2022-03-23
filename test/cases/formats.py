"""interpret the output of `docker-compose` commands"""

import json
import re


def try_match(pattern, subject):
    return re.fullmatch(pattern, subject, re.DOTALL)


def parse_docker_compose_up_line(line):
    # service_log: {service, payload}
    match = try_match(r'(?P<service>\S+)_\d+\s*\| (?P<payload>.*)\n', line)
    if match is not None:
        return ('service_log', {
            'service': match.groupdict()['service'],
            'payload': match.groupdict()['payload']
        })

    # begin_create_container: {container}
    begin_create_container = r'(Rec|C)reating (?P<container>\S+)\s*\.\.\.\s*'
    match = try_match(begin_create_container, line)
    if match is not None:
        return ('begin_create_container', {
            'container': match.groupdict()['container']
        })

    # finish_create_container: {container}
    match = try_match(begin_create_container + r'done\s*', line)
    if match is not None:
        return ('finish_create_container', {
            'container': match.groupdict()['container']
        })

    # attach_to_logs:  {'containers': [container, ...]}
    match = try_match(r'Attaching to (?P<containers>\S+(, \S+)*\s*)', line)
    if match is not None:
        return ('attach_to_logs', {
            'containers': match.groupdict()['containers'].split(', ')
        })

    # image_build_success: {image}
    match = try_match(r'Successfully built (?P<image>\S+)\s*', line)
    if match is not None:
        return ('image_build_success', {'image': match.groupdict()['image']})

    # TODO: Should I assume that `line` ends with "\n" here?
    return ('other', {'payload': line})


def parse_docker_compose_down_line(line):
    match = try_match(r'Removing network (?P<network>\S+)\n', line)
    if match is not None:
        return ('remove_network', {'network': match.groupdict()['network']})

    begin_stop_container = r'Stopping (?P<container>\S+)\s*\.\.\.\s*'
    match = try_match(begin_stop_container, line)
    if match is not None:
        return ('begin_stop_container', {
            'container': match.groupdict()['container']
        })

    match = try_match(begin_stop_container + r'done\s*', line)
    if match is not None:
        return ('end_stop_container', {
            'container': match.groupdict()['container']
        })

    begin_remove_container = r'Removing (?P<container>\S+)\s*\.\.\.\s*'
    match = try_match(begin_remove_container, line)
    if match is not None:
        return ('begin_remove_container', {
            'container': match.groupdict()['container']
        })

    match = try_match(begin_remove_container + r'done\s*', line)
    if match is not None:
        return ('end_remove_container', {
            'container': match.groupdict()['container']
        })

    return ('other', {'payload': line})


def parse_trace(log_line):
    """Return a trace (list of list of dict) parsed from the specified
    `log_line`, or return `None` if `log_line` is not a trace.
    """
    try:
        return json.loads(log_line)
    except json.decoder.JSONDecodeError:
        return None
