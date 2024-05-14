`bin/` (scripts)
----------------
This directory contains programs (scripts) used by the build mostly.

- [cmake\_build.sh](cmake_build.sh) automates creating a build directory for
  CMake, `cd`ing into it, and building the module using cmake.
- [fetch\_docker\_tags.sh](fetch_docker_tags.sh) uses the `hub.docker.com` API
  to retrieve the names of all tags associated with a specified Docker image
  (nginx by default).
- [get\_build\_info\_from\_nginx\_tag.py](get_build_info_from_nginx_tag.py)takes
  an nginx image tag as a command line argument (e.g.
  `./get_build_info_from_nginx_tag.py 1.19.1-alpine`) and prints to standard
  output a JSON object containing information relevant to building an nginx
  module for that image.  Namely:
  - `base_image` the Docker image (with tag) in which nginx was built
  - `configure_args` the arguments passed to `./configure` when nginx was built.
  - `ldd_version` lines of text output by `ldd --version`.
- [nginx\_release\_downloads.sh](nginx_release_downloads.sh) downloads and
  parses `nginx.org/download` to produce a list of nginx versions together with
  a link to the gzipped tarball of the corresponding source release.
- [release.py](release.py) Tags the current HEAD for release, kicks off a
  build/test pipeline on CircleCI, downloads the resulting nginx modules,
  compresses and signs them, and then publishes a draft prerelease to GitHub.
- [generate\_jobs\_yaml.rb](generate_jobs_yaml.rb) prints a snippet of YAML
  that is meant to be added to a "workflow" in CircleCI's
  [config.yml](../.circleci/config.yml).  It saves a lot of typing.
