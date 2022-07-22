`bin/` (scripts)
----------------
This directory contains programs (scripts) used by the build mostly.

- [cmake_build.sh](cmake_build.sh) automates creating a build directory for
  CMake, `cd`ing into it, and building the module using cmake.
- [docker_build.sh](docker_build.sh) creates a Docker image in which the module
  can be built.  It deduces the image's base image and tag by reading the
  `nginx-tag` file at the root of this repository.
- [fetch_nginx_tags.sh](fetch_nginx_tags.sh) uses the `hub.docker.com` API to
  retrieve the names of all tags associated with nginx's nginx Docker image.
- [get_build_info_from_nginx_tag.py](get_build_info_from_nginx_tag.py)  takes
  an nginx image tag as a command line argument (e.g.
  `./get_build_info_from_nginx_tag.py 1.19.1-alpine`) and prints to standard
  output a JSON object containing information relevant to building an nginx
  module for that image.  Namely:
  - `base_image` the Docker image (with tag) in which nginx was built
  - `configure_args` the arguments passed to `./configure` when nginx was built.
- [install_build_tooling_apk.sh](install_build_tooking_apk.sh) installs all of
  the build tooling (compiler, etc.) needed to build the nginx module on a
  system that uses the `apk` package manager (e.g. Alpine Linux).  Notably,
  CMake is built from source, which takes a while.
- [install_build_tooling_apt.sh](install_build_tooling_apt.sh) installs all of
  the build tooling (compiler, etc.) needed to build the nginx module on a
  system that uses the `apt` package manager (e.g. Debian).  Notably, CMake is
  built from source, which takes a while.
- [install_build_tooling.sh](install_build_tooling.sh) `exec`s one of the
  package manager specific scripts above depending on which package manager is
  available.
- [nginx_release_downloads.sh](nginx_release_downloads.sh) downloads and parses
  `nginx.org/download` to produce a list of nginx versions together with a link
  to the gzipped tarball of the corresponding source release.
- [release.sh](release.sh) publishes a draft Github release of this repository.
  It requires the `gh` command line tool in order to interface with Github.  It
  reads from the file `release.json`, which contains a JSON object like the
  following:
    - `release_tag` is the git tag to create on the current tree and to
      associate with the release.
    - `nginx_tags` is an array of nginx Docker image tags.  The module will be
      built for each nginx image so specified.
```json
{
    "release_tag": "v0.3.0",
    "nginx_tags": [
        "1.18.0-alpine",
        "1.19.1"
    ]
}
```
- [run_in_build_image.sh](run_in_build_image.sh) deduces an nginx image from
  the `nginx-tag` file at the root of this repository, and then executes its
  command line arguments in a Docker container from the deduced image.  The
  root of this repository is mounted into the container as `/mnt/repo`.  This
  script is used by the `built-in-docker` target of the `Makefile` at the root
  of this repository. 
