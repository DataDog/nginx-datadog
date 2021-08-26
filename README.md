# TODO: update this

Let's build an nginx module skeleton using cmake, rather than using nginx's
build system (at least not entirely).

[Makefile](Makefile) clones nginx, runs its configuration script, and then
extracts from the generated makefile all source files and include directories
needed for a build, and then produces a `CMakeLists.txt` file that can be
used to build the sources of the module into an archive.

Notes
-----
- On Ubuntu, nginx wants the version of PCRE that's distributed in the package
  "pcre3-dev".  This package is _older than_ PCRE2.  It is a poorly named
  package.  It is _not_ a newer version of PCRE.
