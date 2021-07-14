#!/usr/bin/env python3
"""Parse rules from a Makefile using make.

Assumptions:

- GNU make
- No paths in the Makefile contain whitespace or colons.
- No builtin rules are used by the Makefile.
"""

import contextlib
import json
from pathlib import Path
import re
import subprocess


@contextlib.contextmanager
def from_make(makefile: Path):
    pipe = subprocess.PIPE
    null = subprocess.DEVNULL
    command = [
        'make', '-f', makefile, '--print-data-base', '--dry-run',
        '--no-builtin-rules'
    ]
    with subprocess.Popen(command,
                          encoding='utf8',
                          stdin=null,
                          stdout=pipe,
                          stderr=null) as child:
        yield child.stdout


def is_comment(line):
    return re.match(r'\s*#', line)


def is_recipe(line):
    return line.startswith('\t')


def is_blank(line):
    return line.strip() == ''


_assignment_pattern = r'(\S+)\s*(=|:=|::=|\?=)\s*(.*)'


def is_assignment(line):
    return re.match(_assignment_pattern, line)


def parse_line(line):
    if is_comment(line):
        return 'comment', line
    elif is_recipe(line):
        return 'recipe', line
    elif is_blank(line):
        return 'blank', line
    elif is_assignment(line):
        return 'assignment', parse_assignment(line)
    else:
        return 'rule', parse_rule(line)


def parse_assignment(line):
    variable, assignment_operator, value = re.match(_assignment_pattern,
                                                    line).groups()
    return variable, assignment_operator, value


def parse_rule(line):
    # <target> <target> ... <target>: <dependency> <dependency> <dependency>
    targets, deps = [side.split() for side in line.split(':')]
    return targets, deps


def parse_make_database(makefile: Path):
    with from_make(makefile) as lines:
        for line in lines:
            yield parse_line(line)


def assignments_and_rules(makefile: Path):
    assignments = []
    rules = []
    for line_type, parsed_value in parse_make_database(makefile):
        if line_type == 'assignment':
            assignments.append(parsed_value)
        elif line_type == 'rule':
            rules.append(parsed_value)

    return assignments, rules


def filter_dependencies(rules, file_extension: str):
    for _, dependencies in rules:
        for dependency in dependencies:
            if dependency.endswith(file_extension):
                yield dependency


def filter_assignments(assignments, variable_name: str):
    for variable, _, value in assignments:
        if variable == variable_name:
            yield value


def parse_include_directories(shell_snippet: str):
    for chunk in shell_snippet.split():
        flag, arg = re.match(r'(-I|-iquote|-isystem|-idirafter)?(.*)',
                             chunk).groups()
        if arg:
            yield arg


def all_incs_and_c_sources(makefile):
    assignments, rules = assignments_and_rules(makefile)
    all_incs_var, = filter_assignments(assignments, 'ALL_INCS')
    all_incs = list(parse_include_directories(all_incs_var))
    c_sources = list(filter_dependencies(rules, '.c'))
    return all_incs, c_sources


if __name__ == '__main__':
    import sys
    all_incs, c_sources = all_incs_and_c_sources(sys.argv[1])
    json.dump({
        'all_incs': all_incs,
        'c_sources': c_sources
    }, sys.stdout, indent=2)
