#!/usr/bin/env python3
"""Parse rules from a Makefile using make.

Assumptions:

- GNU make
- No paths in the Makefile contain whitespace or colons.
- No builtin rules are used by the Makefile.
"""

import contextlib
from pathlib import Path
import re
import subprocess


@contextlib.contextmanager
def from_make(makefile: Path):
    """TODO: document"""
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
    # FOO = bar
    # FOO := bar
    # FOO ::= bar
    # FOO ?= bar
    return re.match(_assignment_pattern, line) is not None


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
    # FOO = bar
    # FOO := bar
    # FOO ::= bar
    # FOO ?= bar
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
            parsed_line = parse_line(line)
            yield parse_line(line)


def direct_dependencies(rules):
    # [foo: bar baz, bar: baz]  ->  {foo: {bar, baz}, bar: {baz}}
    deps_dict = {}
    for targets, deps in rules:
        for target in targets:
            deps_dict.setdefault(target, set()).update(deps)

    return deps_dict


def depth_first(deps_dict, root):
    """TODO: document"""
    for dep in deps_dict.get(root, []):
        yield from depth_first(deps_dict, dep)
    yield root


def parse_include_args(args):
    """TODO: document"""
    for arg in args:
        flag, path = re.match(r'^(-I|-iquote|-isystem|-idirafter)?(.*)$', arg).groups()
        if path:
            yield path


if __name__ == '__main__':
    """TODO: document"""
    import json
    import sys

    makefile = Path(sys.argv[1])
    module_name = sys.argv[2]

    all_incs = []
    rules = []
    for element in parse_make_database(makefile):
        kind, value = element
        if kind == 'rule':
            rules.append(value)
        elif kind == 'assignment':
            variable_name, _, variable_value = value
            if variable_name == 'ALL_INCS':
                for path in parse_include_args(variable_value.split()):
                    # Parse as Path and then stringify, so the value is canonical.
                    # It allows us to mix these values with other Path-derived
                    # strings.
                    all_incs.append(str(Path(path)))

    deps_dict = direct_dependencies(rules)

    include_directories = set(all_incs)
    c_sources = set()
    for file in depth_first(deps_dict, f'objs/{module_name}.so'):
        path = Path(file)
        if path.suffix == '.h':
            include_directories.add(str(path.parent))
        elif path.suffix == '.c':
            c_sources.add(file)

    json.dump({
        'include_directories': list(include_directories),
        'c_sources': list(c_sources)
    }, sys.stdout, indent=2)
