Let's build an nginx module skeleton using cmake, rather than using nginx's
build system (at least not entirely).

[Makefile](Makefile) clones nginx, runs its configuration script, and then
extracts from the generated makefile all source files and include directories
needed for a build, and then produces a `CMakeLists.txt` file that can be
used to build the sources of the module into an archive.

