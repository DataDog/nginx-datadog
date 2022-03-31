Features
--------
- The goal is to minimize required configuration, by choosing suitable
  defaults, and to allow some Datadog-specific conveniences:
  - `proxy_pass`, `fastcgi_pass`, and `grpc_pass` are automatically traced,
    unless `datadog_disable` is used.
      - `datadog_enable` is the opposite of `datadog_disable`.  Tracing is
        enabled by default.
  - The default access log format is one where the current trace ID and span ID
    are included, or "-" if tracing is disabled.
  - If the tracer is not configured with a `datadog { ... }` directive, then a
    default configuration is used instead.
  - Environment variables used by the tracer (e.g. `DD_ENV`) are automatically
    forwarded to worker child processes.
  - `$datadog_trace_id` and `$datadog_span_id` are available as variables.
    They expand to the corresponding ID or to "-" if tracing is disabled.
    `$datadog_json` is also available as a JSON dump of the propagation
    context.
  - The common onboarding experience is intended to be: "just load the module."
- The code is originally based on `nginx-opentracing` (copy → paste → modify).
  One of the things I did was effectively `s/opentracing/datadog/g`, so many of
  the files, functions, and types in this module have a similarly-named cousin
  in `nginx-opentracing`. 
- Instead of loading an OpenTracing plugin, the module consults an in-source
  collection of functions, `TracingLibrary`.  This keeps the "policy" separate
  from where it is used within nginx, though there is no longer any dynamic
  loading.
- The tracer configuration previously loaded from a file, e.g.
  `dd-config.json`, is now added to the nginx config itself (still as JSON).
  This requires some special config parsing (including a JSON library).
- Automatic tracing of proxy directives (`*_pass`) is achieved by hijacking the
  proxy directive, replacing it with one that dispatches to the original
  handler first and then inserts the corresponding tracing directive (e.g.
  `datadog_fastcgi_propagate_context`).
  - In order to "hijack" in this way, the Datadog module needs to register a
    handler for the hijacked configuration directive, and additionally make
    sure that its handler is called preferentially to the original one.  This
    is achieved by listing the Datadog module _before_ those modules it
    impersonates.  Nginx's build configuration system allows this.  See
    [bin/module_config.sh](bin/module_config.sh).
- Environment variable forwarding is handled in a hook that is invoked in the
  main process after configuration is complete (see
  `datadog_master_process_post_config`).
- Access to trace ID, span ID, and context propagation headers is provided
  using two nginx variable prefixes: `datadog_` for span properties, and
  `datadog_propagation_header_` for propagation headers.  Look for calls to
  `TracingLibrary::span_variables` and
  `TracingLibrary::propagation_header_variable_name_prefix`.
- The default access log format is altered by defining a new log format,
  "`datadog_text`", and then hijacking the `access_log` directive so that its
  default format is "`datadog_text`" instead of "`combined`".  This means that
  a user must mention the `access_log` directive in the nginx config to get the
  modified behavior.  Fortunately, the `nginx.conf` included with packaged
  versions of nginx always uses `access_log`, as does every `nginx.conf` I've
  seen.

Build Dependency Management
---------------------------
```text
         msgpack      curl      zlib?       
            -\         |          -         
              --\      |        -/          
                 -\    |      -/            
                   --  |     /              
               -- dd-opentracing-cpp        
            --/             -\              
         --/                  ---\          
       -/                         --        
 nginx-datadog ------------ opentracing-cpp 
      -\                                    
        -\                                  
          --\                               
             -\                             
               -- nginx                     
```
- `nginx-datadog` is this project.
- `dd-opentracing-cpp` and `opentracing-cpp` are both included in-source-tree
  as git submodules.
- `dd-opentracing-cpp`'s third-party build dependencies are installed as part
  part of the build.
- `dd-opentracing-cpp` and `opentracing-cpp` have both been modified to expose
  cmake libraries of type `OBJECT`, which means a build can import the
  compilation and linkage commands from the project without producing an
  intermediate library artifact (`.so`, `.a`).  This way, the entire build is
  one cmake pass that compiles all sources, thus ensuring that the toolchain is
  consistent among `nginx-datadog` / `nginx` / `opentracing-cpp` /
  `dd-opentracing-cpp`.
- [CMakeLists.txt](CMakeLists.txt) builds the nginx module `.so`.  In order to do so,
  it includes:
  - `nginx-module.cmake`: the nginx-specific sources read from `nginx/`
  - `nginx-datadog.cmake`: the bulk of the module source code, i.e. `src/`
  - `opentracing-cpp/CMakeLists.txt`
  - `dd-opentracing-cpp/CMakeLists.txt`

### Building Nginx
Nginx's source tree is not part of this repository, since we want to produce
different builds for different nginx versions.  Instead, an nginx source
release tarball is downloaded as part of the build process.  The version to
clone is determined by the file [nginx-version](nginx-version) (which may be
modified at the beginning of the build process).

Nginx is not meant to be built using cmake.  It has its own custom `configure`
script that produces a platform-specific makefile, `objs/Makefile`.

Nginx is not meant to be built as a library.  When building an nginx module,
nginx's (C language) build system "drives the build."  `nginx-opentracing`
snuck C++ into the mix by altering `make` variables outside of that allowed by
the build contract between nginx and its modules (but nobody was harmed)
They also relinked the resulting shared object in order to include the C++
standard library.

For `nginx-datadog`, I wanted nginx to be a cmake library of type `OBJECT`,
like the other dependencies, so that it could be part of a single build in the
same way.  No sneaking variables into the nginx build, no relinking.

One option is to port nginx's build system to cmake.  There's at least one
project on github that claims to do this.  I don't like it.

Another option is to hand-maintain a `CMakeLists.txt` specific to the nginx
source tree.  This wouldn't be too bad, except for nginx's `configure` step:
the contents of the `CMakeLists.txt` would depend on the output of `configure`,
which is platform dependent.

_[edit: The nginx cmake configuration is no longer generated by the build.]_

The option I chose is to _generate_ `CMakeLists.txt` from the output of
`configure`.  `configure` produces `objs/Makefile`, which contains only one
target of interest: `objs/ngx_http_datadog_module.so`.  Instead of producing
that shared library, we want to collect all of the requisite source files and
header files, and put _them_ in the `CMakeLists.txt`.

GNU Make has an option, `--print-data-base`, that outputs the dependency graph
of all targets and the values of all variables in a fixed format.  Then it's
just a matter of finding the `objs/ngx_http_datadog_module.so` vertex, and
traversing the graph to find all `.c` and `.h` dependencies.  The `.c` files
are then what the `CMakeLists.txt` will add to the build, and the directories
of the `.h` files will be included in compilation flags.

This _almost_ works.  It fails because there's something like a bug in the
nginx build system.  It's not so much a bug as a bit of dishonesty.  The
`objs/Makefile` generated by `configure` declares a variable containing headers
used in "core" modules:
```make
CORE_DEPS = src/core/nginx.h \
	src/core/ngx_config.h \
	src/core/ngx_core.h \
    [...]
```
and another variable containing headers specific to "http" modules:
```make
HTTP_DEPS = src/http/ngx_http.h \
	src/http/ngx_http_request.h \
	src/http/ngx_http_config.h \
    [...]
```
Built-in HTTP modules then list both variables as dependencies.  However, the
Datadog module, despite being an HTTP module, list only `CORE_DEPS` as a
dependency:
```make
objs/ngx_http_datadog_module_modules.o:	$(CORE_DEPS) [...]
```
The reason the code still builds correctly is that the compilation commands
themselves reference yet another variable, `$(ALL_INCS)` that contains _all_ of
the directories containing header files.

So, strictly speaking, you could modify an HTTP-specific nginx header file, and
the generated `Makefile` would neglect to recompile the Datadog module.  With a
clean build, though, it doesn't matter.

All this to say that the graph traversal described above is not enough to
discover all of the build dependencies of the module.  We must also fetch the
value of the `$(ALL_INCS)` variable and include those directories.

That is what [bin/makefile_database.py](bin/makefile_database.py) and
[bin/generate_cmakelists.py](bin/generate_cmakelists.py) together do.  The
result is `nginx-module.cmake`, which exposes a
```cmake
add_library(nginx_module OBJECT)
```
that draws from sources under `nginx/`.

As it turns out, there's only _one_ source file to compile.  Also, nginx
currently supports only the "unix" flavor of build, and so the platform
independence that this "read the `Makefile`" technique allows will probably not
be needed.  I might remove the machinery and instead hard-code
`nginx-module.cmake` in the source tree.  _[edit: I did.]_
