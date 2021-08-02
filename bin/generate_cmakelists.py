#!/usr/bin/env python3
"""Generate CMakeLists.txt for an nginx module build

TODO command line argument

Write a CMakeLists.txt file to standard output that describes a static
("object") build of an nginx module.

Read JSON from standard input that contains the source files and include
directories relevant to the build.
"""

import json
from pathlib import Path
import sys

target_name = sys.argv[1]
build_info = json.load(sys.stdin)
all_incs, c_sources = build_info['all_incs'], build_info['c_sources']

# Prefix the paths with the path to the nginx repository directory (./nginx).
nginx = Path('nginx')
all_incs = [str(nginx / path) for path in all_incs]
c_sources = [str(nginx / path) for path in c_sources]

template = """\
cmake_minimum_required(VERSION 3.7)

project({target_name})

add_library({target_name} OBJECT)
set_property(TARGET {target_name} PROPERTY POSITION_INDEPENDENT_CODE ON)

target_sources({target_name}
    PRIVATE
{sources_indent}{nginx}/objs/ngx_http_opentracing_module_modules.c
)

include_directories(
    SYSTEM
{includes_indent}{includes}
)
"""

sources_indent = ' ' * 8
includes_indent = sources_indent

print(
    template.format(target_name=target_name,
                    sources_indent=sources_indent,
                    nginx=str(nginx),
                    includes_indent=includes_indent,
                    includes=('\n' + includes_indent).join(all_incs)))
