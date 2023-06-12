Laboratory
==========
This directory is like [../test/](../test/), except rather than being for
running integration tests, it's for playing around with a locally-built
module during development.

It's a docker compose setup that contains an nginx with the built module, a fake
datadog agent, an HTTP upstream, gRPC upstream, and fastCGI upstream.

The underlying nginx docker image is determined by `../nginx-version-info`.

There's a `make` target that builds the module in docker, copies it into `lab/`, and runs
`docker compose up --build`. At the top of the repository, run:
```console
$ make lab
```
