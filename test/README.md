Tests
=====
These are integration tests.  They're written in python, and use
`docker compose` to orchestrate an instance of nginx containing the module
under test, and other services reverse proxied by nginx.

See the readme file in [cases/](cases/) for usage information.

The docker image of the tests is controlled by `nginx-version-info`. The `WAF`
environment variable controls whether the tests related to AppSec are run (value
`ON`) or skipped (otherwise).

Files
-----
- [bin/](bin/) contains scripts for running and developing the tests.  Notably,
  [bin/run](bin/run) runs the tests.
- [cases/](cases/) contains the actual python test cases that run tests against
  the `docker compose` setup.
- [services/](services/) contains the dockerfiles for the `docker compose`
  services, and other data relevant to the services.
- [docker-compose.yaml](docker-compose.yml) defines the services used by the
  tests.
