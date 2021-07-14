#!/usr/bin/env python3
"""Generate CMakeLists.txt for an nginx module build

TODO command line argument

Write a CMakeLists.txt file to standard output that describes a static build of
an nginx module.

Read JSON from standard input that contains the source files and include
directories relevant to the build.
"""

import json
from pathlib import Path
import sys

module_name = sys.argv[1]
build_info = json.load(sys.stdin)
all_incs, c_sources = build_info['all_incs'], build_info['c_sources']

# Prefix the paths with the path to the nginx repository directory (./nginx).
nginx = Path('nginx')
all_incs = [str(nginx / path) for path in all_incs]
c_sources = [str(nginx / path) for path in c_sources]

template = """\
cmake_minimum_required(VERSION 3.7)

project({module_name})

add_library({module_name} STATIC)

target_sources({module_name}
    PRIVATE
{sources_indent}{sources}
)

include_directories(
    SYSTEM
{includes_indent}{includes}
)
"""

sources_indent = ' ' * 8
includes_indent = sources_indent

print(
    template.format(module_name=module_name,
                    sources_indent=sources_indent,
                    sources=('\n' + sources_indent).join(c_sources),
                    includes_indent=includes_indent,
                    includes=('\n' + includes_indent).join(all_incs)))
