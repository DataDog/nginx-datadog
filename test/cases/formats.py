"""interpret the output of `docker compose` commands"""

import json
import re


def try_match(pattern, subject):
    return re.fullmatch(pattern, subject, re.DOTALL)


def parse_docker_compose_down_line(line):
    match = try_match(r'\s*Removing network (?P<network>\S+)\n', line)
    if match is not None:
        return ('remove_network', {'network': match.groupdict()['network']})

    begin_stop_container = r'\s*Stopping (?P<container>\S+)\s*\.\.\.\s*'
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

    begin_remove_container = r'\s*Removing (?P<container>\S+)\s*\.\.\.\s*'
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
    `log_line`, or return `None` if `log_line` is not a v0.4 trace.

    Rejects v0.5-format payloads (string-table + integer-encoded spans) that
    other tracers (e.g. dd-trace-py / uwsgi) may send to the same agent port.
    Those have a list of strings as their first element rather than a list of
    span dicts.
    """
    try:
        trace = json.loads(log_line)
    except json.decoder.JSONDecodeError:
        return None

    if not isinstance(trace, list):
        return None

    if not all(
            isinstance(chunk, list) and all(
                isinstance(span, dict) for span in chunk) for chunk in trace):
        return None

    return trace


def parse_spans(log_lines):
    """Return a list of spans parsed from some subset of the specified
    `log_lines`.
    """
    spans = []
    for line in log_lines:
        trace = parse_trace(line)
        if trace is None:
            continue
        for chunk in trace:
            for span in chunk:
                spans.append(span)
    return spans


def parse_rem_cfg_request(log_lines):
    """Returns a list of decoded remote config requests seen by the agent.
    They look like 'Remote config request with version 1712676390: <json data>"""

    requests = []
    for line in log_lines:
        match = try_match(
            r'Remote config request with version \d+: (?P<json>.+)', line)
        if match is not None:
            requests.append(json.loads(match.groupdict()['json']))
    return requests


def parse_access_log_sync_line(log_line):
    # Return a `dict` containing the sync token parsed from the specified
    # `log_line` if `log_line` is from the nginx access log for a sync request.
    # Otherwise, return `None`.
    match = try_match(rf'.*"GET /sync\?token=(?P<token>\S+) HTTP/1\.1".*',
                      log_line)
    if match is not None:
        return {'token': match.groupdict()['token']}
